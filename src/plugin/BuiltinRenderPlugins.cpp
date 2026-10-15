#include "plugin/RenderPluginRegistry.h"

#if IRIS_ENABLE_ENSCAPE_PLUGIN
#include "EnscapeCubeRenderer.h"
#endif
#if IRIS_ENABLE_NPR_PLUGIN
#include "NprRenderer.h"
#endif
#if IRIS_ENABLE_WATER_PLUGIN
#include "WaterRenderer.h"
#endif
#if IRIS_ENABLE_POSTPROCESS_PLUGIN
#include "PostProcessor.h"
#endif

namespace iris {

const std::vector<RenderPluginDescriptor>& builtinRenderPluginCatalog() {
    static const std::vector<RenderPluginDescriptor> catalog={
        {enscapePluginId,renderPluginApiVersion,{openGlFullscreenService},"CC BY-NC-SA-3.0 (third-party shaders)",enscapeContract(),builtinRenderPluginParameterSchema(enscapePluginId)},
        {postProcessPluginId,renderPluginApiVersion,{openGlFullscreenService},"MIT",postProcessContract(),builtinRenderPluginParameterSchema(postProcessPluginId)},
        {nprPluginId,renderPluginApiVersion,{openGlShadingService},"MIT",{PluginStage::MaterialShading,{},{}},builtinRenderPluginParameterSchema(nprPluginId)},
        {waterPluginId,renderPluginApiVersion,{openGlShadingService,openGlIndexedGeometryService,openGlShaderBorrowService,openGlIndexedDrawService},"MIT",{PluginStage::Surface,{},{}},builtinRenderPluginParameterSchema(waterPluginId)}};
    return catalog;
}

const RenderPluginRegistry& builtinRenderPlugins() {
    static const RenderPluginRegistry registry = [] {
        RenderPluginRegistry result;
#if IRIS_ENABLE_ENSCAPE_PLUGIN
        result.add(builtinRenderPluginCatalog()[0],
            [](const std::filesystem::path& directory) {
                return std::make_unique<EnscapeCubeRenderer>(directory);
            });
#endif
#if IRIS_ENABLE_NPR_PLUGIN
        result.add(builtinRenderPluginCatalog()[2], [](const std::filesystem::path&) {
            return std::make_unique<NprRenderer>();
        });
#endif
#if IRIS_ENABLE_WATER_PLUGIN
        result.add(builtinRenderPluginCatalog()[3], [](const std::filesystem::path& directory) {
            return std::make_unique<WaterRenderer>(directory);
        });
#endif
#if IRIS_ENABLE_POSTPROCESS_PLUGIN
        result.add(builtinRenderPluginCatalog()[1],
            [](const std::filesystem::path& directory) {
                return std::make_unique<PostProcessor>(directory / "fullscreen.vert",
                    directory / "bloom_extract.frag", directory / "bloom_blur.frag",
                    directory / "postprocess.frag", directory / "temporal_aa.frag");
            });
#endif
        return result;
    }();
    return registry;
}

} // namespace iris
