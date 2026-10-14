#include "WaterRenderer.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include <stdexcept>
namespace iris {
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
