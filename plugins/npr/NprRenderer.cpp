#include "NprRenderer.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "render/PostProcessSettings.h"
#include <algorithm>
#include <stdexcept>

namespace iris {
void NprRenderer::applyPostProcessSettings(const RendererSettings& settings, PostProcessSettings& post) const {
    const bool stylized = settings.shadingMode == ShadingMode::Stylized;
    post.outline = stylized && settings.stylizedOutlineEnabled;
    post.outlineWidth = settings.stylizedOutlineWidth;
    post.outlineDepthThreshold = settings.stylizedOutlineDepthThreshold;
    post.outlineNormalThreshold = settings.stylizedOutlineNormalThreshold;
    post.outlineColor = settings.stylizedOutlineColor;
    post.dither = stylized && settings.stylizedDitherEnabled;
    post.ditherStrength = settings.stylizedDitherStrength;
    post.heightFog = stylized && settings.stylizedHeightFogEnabled;
    post.heightFogDensity = settings.stylizedHeightFogDensity;
    post.heightFogBaseHeight = settings.stylizedHeightFogBaseHeight;
    post.heightFogFalloff = settings.stylizedHeightFogFalloff;
    post.heightFogColor = settings.stylizedHeightFogColor;
    post.colorGrading = stylized && settings.stylizedColorGradingEnabled;
    post.colorGradingLut = static_cast<int>(settings.stylizedColorGradingLut);
    post.colorGradingStrength = settings.stylizedColorGradingStrength;
    post.stylizedDebugView = stylized ? static_cast<int>(settings.stylizedDebugView)
        : static_cast<int>(StylizedDebugView::Final);
}
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
