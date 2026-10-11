#include "plugin/RenderPlugin.h"
#include "render/RenderTarget.h"
#include "render/PostProcessSettings.h"
#include "plugin/RenderPluginParameters.h"

namespace iris {
std::vector<ModuleParameterOverride> RenderPluginFrame::parameterValues(const RenderPluginParameterSchema& schema) const {
    return schema.capture(settings);
}
unsigned int RenderPluginFrame::texture(const std::string& name) const {
    for (const auto& binding : textures) if (binding.name == name) return binding.texture;
    return 0U;
}
RenderPluginFrame::RenderPluginFrame(RenderTarget& targetValue, const Camera& cameraValue,
    const RendererSettings& settingsValue, int widthValue, int heightValue, float timeValue,
    unsigned int vao, const PostProcessSettings* postSettings)
    : target(targetValue), camera(cameraValue), settings(settingsValue), width(widthValue),
      height(heightValue), timeSeconds(timeValue), fullscreenVertexArray(vao),
      postProcessSettings(postSettings) {
    textures.push_back({"display",target.colorTexture(),TextureFormat::DisplayColor,target.width(),target.height()});
    if (!postSettings) return;
    textures.push_back({"hdr",target.hdrColorTexture(),TextureFormat::HdrColor,target.width(),target.height()});
    textures.push_back({"depth",postSettings->depthTexture,TextureFormat::Depth,width,height});
    textures.push_back({"motion",postSettings->objectMotionTexture,TextureFormat::Data,width,height});
    textures.push_back({"normals",postSettings->outlineNormalTexture,TextureFormat::Data,width,height});
    textures.push_back({"cloud",postSettings->cloudTexture,TextureFormat::HdrColor,postSettings->cloudWidth,postSettings->cloudHeight});
    textures.push_back({"cloudDepth",postSettings->cloudDepthTexture,TextureFormat::Data,postSettings->cloudWidth,postSettings->cloudHeight});
    textures.push_back({"rays",postSettings->godRaysTexture,TextureFormat::Data,postSettings->godRaysWidth,postSettings->godRaysHeight});
    textures.push_back({"opaqueDepth",postSettings->opaqueDepthTexture,TextureFormat::Depth,width,height});
}
} // namespace iris
