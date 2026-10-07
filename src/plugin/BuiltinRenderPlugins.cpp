#include "plugin/RenderPluginRegistry.h"

#if IRIS_ENABLE_ENSCAPE_PLUGIN
#include "EnscapeCubeRenderer.h"
#endif

namespace iris {

const RenderPluginRegistry& builtinRenderPlugins() {
    static const RenderPluginRegistry registry = [] {
        RenderPluginRegistry result;
#if IRIS_ENABLE_ENSCAPE_PLUGIN
        result.add({enscapePluginId, renderPluginApiVersion,
            {openGlFullscreenService}, "CC BY-NC-SA-3.0 (third-party shaders)"},
            [](const std::filesystem::path& directory) {
                return std::make_unique<EnscapeCubeRenderer>(directory);
            });
#endif
        return result;
    }();
    return registry;
}

} // namespace iris
