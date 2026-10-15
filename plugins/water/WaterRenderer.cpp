#include "WaterRenderer.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "render/Camera.h"
#include "render/WaterSurface.h"
#include <glad/gl.h>
#include <glm/matrix.hpp>
#include <cmath>
#include <stdexcept>
namespace iris {
WaterRenderer::WaterRenderer(std::filesystem::path shaderDirectory) : shaderDirectory_(std::move(shaderDirectory)) {}
WaterRenderer::~WaterRenderer() = default;
void WaterRenderer::prepareResources(const RendererSettings&, int, int) {
    if (high_ && low_ && forwardShader_ && deferredShader_) return;
    auto forward = std::make_unique<Shader>(shaderDirectory_ / "water.vert", shaderDirectory_ / "water_forward.frag");
    auto deferred = std::make_unique<Shader>(shaderDirectory_ / "water.vert", shaderDirectory_ / "water_gbuffer.frag");
    auto high = std::make_unique<WaterSurface>(water::gridResolution);
    auto low = std::make_unique<WaterSurface>(water::lowGridResolution);
    if (!forward->sourcesCurrent() || !deferred->sourcesCurrent())
        throw std::runtime_error("Water shader sources changed during preparation");
    forwardShader_ = std::move(forward);
    deferredShader_ = std::move(deferred);
    high_ = std::move(high);
    low_ = std::move(low);
}
Shader* WaterRenderer::borrowShadingShader(bool deferred) const {
    auto* shader = deferred ? deferredShader_.get() : forwardShader_.get();
    if (!shader) throw std::runtime_error("Water shader has not been prepared");
    return shader;
}
IndexedTriangleGeometry WaterRenderer::borrowIndexedGeometry(const RendererSettings& settings) const {
    const auto* surface = settings.water.quality == WaterQuality::Low ? low_.get() : high_.get();
    if (!surface) throw std::runtime_error("Water geometry has not been prepared");
    return {surface->vertexArray(), surface->indexCount()};
}
std::size_t WaterRenderer::estimatedBytes() const {
    return (high_ ? high_->estimatedBytes() : 0) + (low_ ? low_->estimatedBytes() : 0);
}
std::size_t WaterRenderer::drawIndexedGeometry(const RendererSettings& settings) const {
    const auto geometry = borrowIndexedGeometry(settings);
    validateIndexedTriangleGeometry(geometry);
    glBindVertexArray(geometry.vertexArray);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(geometry.indexCount), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    return 1;
}
void WaterRenderer::renderFrame(const RenderPluginFrame& frame) {
    if (!frame.shadingShader || (frame.shadingShader != forwardShader_.get() && frame.shadingShader != deferredShader_.get()))
        throw std::invalid_argument("Water requires its borrowed, bound water shader");
    auto& shader=*frame.shadingShader;
    const auto& settings=frame.settings.water;
    // Deferred also uses this program for water color; only its motion subpass
    // uses deferredShader_. Select by the actual borrowed program, not settings.
    if (frame.waterInputs && frame.shadingShader == forwardShader_.get()) {
        if (frame.width < 1 || frame.height < 1)
            throw std::invalid_argument("Water color frame requires positive dimensions");
        const auto inverse=glm::inverse(frame.waterInputs->currentViewProjection);
        for(int column=0;column<4;++column)for(int row=0;row<4;++row)
            if(!std::isfinite(inverse[column][row]))
                throw std::invalid_argument("Water color frame requires an invertible view-projection");
        shader.setVec3("uCameraForward",frame.camera.forwardDirection());
        shader.setFloat("uInverseViewportWidth",1.0f/static_cast<float>(frame.width));
        shader.setFloat("uInverseViewportHeight",1.0f/static_cast<float>(frame.height));
        shader.setFloat("uCameraNearPlane",frame.camera.nearPlane());
        shader.setFloat("uCameraFarPlane",frame.camera.farPlane());
        shader.setMat4("uInverseCurrentViewProjection",inverse);
    }
    if (frame.waterLightingInputs && frame.shadingShader == forwardShader_.get()) {
        const auto& light=*frame.waterLightingInputs;
        shader.setVec3("uLightDirection",light.lightDirection);
        shader.setVec3("uLightColor",light.lightColor);
        shader.setBool("uSeparatedWaterSun",settings.surfaceOptics && frame.settings.atmosphere.enabled);
        shader.setVec3("uWaterKeyIrradiance",light.keyIrradiance);
        shader.setFloat("uDiffuseStrength",light.diffuseStrength);
        shader.setFloat("uEnvironmentIntensity",frame.settings.environmentIntensity);
        shader.setFloat("uEnvironmentMaxMip",light.environmentMaxMip);
        shader.setFloat("uTwilightFactor",light.twilightFactor);
    }
    if (frame.waterInputs) {
        const auto& input=*frame.waterInputs;
        shader.setMat4("uCurrentViewProjection",input.currentViewProjection);
        shader.setMat4("uPreviousViewProjection",input.previousViewProjection);
        shader.setVec3("uCameraPosition",frame.camera.position());
        shader.setFloat("uTime",settings.timeSeconds);
        shader.setFloat("uPreviousTime",input.previousTimeSeconds);
        shader.setFloat("uCameraSurfaceHeight",input.cameraSurfaceHeight);
        shader.setBool("uMotionHistoryValid",input.motionHistoryValid);
    }
    shader.setFloat("uExtent",settings.extent);
    shader.setFloat("uNearMeshFocus",settings.nearMeshFocus);
    shader.setFloat("uGridResolution",static_cast<float>(settings.quality==WaterQuality::Low ? water::lowGridResolution : water::gridResolution));
    shader.setFloat("uLevel",settings.level);
    shader.setFloat("uSpeed",settings.speed);
    shader.setFloat("uSteepness",settings.steepness);
    shader.setFloat("uFoamStrength",settings.foamStrength);
    shader.setFloat("uRoughness",settings.roughness);
    shader.setFloat("uReflectionStrength",settings.reflectionStrength);
    shader.setFloat("uRippleStrength",settings.rippleStrength);
    shader.setFloat("uSunGlintStrength",settings.sunGlintStrength);
    shader.setFloat("uDeepWaterStrength",settings.deepWaterStrength);
    shader.setBool("uSurfaceOptics",settings.surfaceOptics);
    shader.setFloat("uWaveDiversity",settings.waveDiversity);
    const auto waves=water::components(settings);
    shader.setVec4Array("uWaves[0]",waves.data(),waves.size());
    shader.setInt("uWaveCount",water::activeComponentCount(settings));
    shader.setFloat("uCloudReflectionStrength",settings.cloudReflectionStrength);
    shader.setBool("uHighQuality",settings.quality==WaterQuality::High);
}
}
