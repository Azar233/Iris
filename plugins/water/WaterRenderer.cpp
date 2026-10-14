#include "WaterRenderer.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "render/WaterSurface.h"
#include <stdexcept>
namespace iris {
WaterRenderer::WaterRenderer() = default;
WaterRenderer::~WaterRenderer() = default;
void WaterRenderer::prepareResources(const RendererSettings&, int, int) {
    if (high_ && low_) return;
    auto high = std::make_unique<WaterSurface>(water::gridResolution);
    auto low = std::make_unique<WaterSurface>(water::lowGridResolution);
    high_ = std::move(high);
    low_ = std::move(low);
}
IndexedTriangleGeometry WaterRenderer::borrowIndexedGeometry(const RendererSettings& settings) const {
    const auto* surface = settings.water.quality == WaterQuality::Low ? low_.get() : high_.get();
    if (!surface) throw std::runtime_error("Water geometry has not been prepared");
    return {surface->vertexArray(), surface->indexCount()};
}
std::size_t WaterRenderer::estimatedBytes() const {
    return (high_ ? high_->estimatedBytes() : 0) + (low_ ? low_->estimatedBytes() : 0);
}
void WaterRenderer::renderFrame(const RenderPluginFrame& frame) {
    if (!frame.shadingShader) throw std::invalid_argument("Water requires a bound water shader");
    auto& shader=*frame.shadingShader;
    const auto& settings=frame.settings.water;
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
