#pragma once
#include <string>
#include <vector>

namespace iris {
enum class PluginStage { FullscreenScene, PostProcess };
enum class TextureFormat { HdrColor, DisplayColor, Depth, Data };
enum class ResourceScope { Input, Output, Transient, History };
struct PluginResource {
    std::string name;
    TextureFormat format;
    ResourceScope scope;
    bool required{true};
};
struct PluginPass {
    std::string name;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
    std::vector<std::string> previousFrameReads;
};
struct RenderPluginContract {
    PluginStage stage{PluginStage::FullscreenScene};
    std::vector<PluginResource> resources;
    std::vector<PluginPass> passes;
};
struct RenderTextureBinding {
    std::string name;
    unsigned int texture;
    TextureFormat format;
    int width;
    int height;
};
// Ordered logical pass plans: reject ambiguous writers, unproduced reads and
// same-frame cycles. Private blur loops/ping-pong remain owned by the plugin.
void validatePluginContract(const RenderPluginContract& contract);
void validatePluginBindings(const RenderPluginContract& contract,
    const std::vector<RenderTextureBinding>& bindings, int width, int height);
RenderPluginContract enscapeContract();
RenderPluginContract postProcessContract();
} // namespace iris
