#include "plugin/RenderPluginRegistry.h"

#if IRIS_ENABLE_ENSCAPE_PLUGIN
#include "EnscapeCubeRenderer.h"
#endif
#if IRIS_ENABLE_POSTPROCESS_PLUGIN
#include "PostProcessor.h"
#endif

namespace iris {

const RenderPluginRegistry& builtinRenderPlugins() {
    static const RenderPluginRegistry registry = [] {
        RenderPluginRegistry result;
#if IRIS_ENABLE_ENSCAPE_PLUGIN
        result.add({enscapePluginId, renderPluginApiVersion,
            {openGlFullscreenService}, "CC BY-NC-SA-3.0 (third-party shaders)", enscapeContract()},
            [](const std::filesystem::path& directory) {
                return std::make_unique<EnscapeCubeRenderer>(directory);
            });
#endif
#if IRIS_ENABLE_POSTPROCESS_PLUGIN
        result.add({postProcessPluginId, renderPluginApiVersion,
            {openGlFullscreenService}, "MIT", postProcessContract()},
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
