#include "NprRenderer.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include <algorithm>
#include <stdexcept>

namespace iris {
void NprRenderer::renderFrame(const RenderPluginFrame& frame) {
    if (!frame.shadingShader) throw std::invalid_argument("NPR requires a bound material shader");
    auto& shader = *frame.shadingShader;
    const auto& settings = frame.settings;
    shader.setBool("uStylizedEnabled", settings.shadingMode == ShadingMode::Stylized);
    shader.setInt("uStylizedBandCount", std::clamp(settings.stylizedBandCount, 2, 8));
    shader.setFloat("uStylizedBandSoftness", settings.stylizedBandSoftness);
    shader.setFloat("uStylizedSpecularSize", settings.stylizedSpecularSize);
    shader.setFloat("uStylizedSpecularSoftness", settings.stylizedSpecularSoftness);
    shader.setFloat("uStylizedRimWidth", settings.stylizedRimWidth);
    shader.setFloat("uStylizedRimSoftness", settings.stylizedRimSoftness);
    shader.setFloat("uStylizedRimIntensity", settings.stylizedRimIntensity);
    shader.setVec3("uStylizedShadowTint", settings.stylizedShadowTint);
    shader.setVec3("uStylizedRimColor", settings.stylizedRimColor);
    shader.setInt("uStylizedDebugView", static_cast<int>(settings.stylizedDebugView));
}
}
