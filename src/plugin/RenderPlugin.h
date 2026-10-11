#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "plugin/RenderPluginContract.h"
#include "module/ParameterRegistry.h"

class Camera;
class RenderTarget;
struct RendererSettings;
struct PostProcessSettings;

namespace iris {
struct RenderPluginParameterSchema;

inline constexpr int renderPluginApiVersion = 2;
inline constexpr const char* enscapePluginId = "iris.enscape-study";
inline constexpr const char* postProcessPluginId = "iris.postprocess";
inline constexpr const char* openGlFullscreenService = "opengl.fullscreen.v1";

// Borrowed frame inputs. The caller owns the context, target and scheduling.
// RendererSettings is a compatibility bridge for this first migration, not a
// frozen public material/plugin schema or a cross-backend GPU ABI.
struct RenderPluginFrame {
    RenderPluginFrame(RenderTarget& target, const Camera& camera, const RendererSettings& settings,
        int width, int height, float timeSeconds, unsigned int fullscreenVertexArray,
        const PostProcessSettings* postProcessSettings = nullptr);
    RenderTarget& target;
    const Camera& camera;
    const RendererSettings& settings;
    int width;
    int height;
    float timeSeconds;
    unsigned int fullscreenVertexArray;
    const PostProcessSettings* postProcessSettings;
    std::vector<RenderTextureBinding> textures;
    unsigned int texture(const std::string& name) const;
    std::vector<ModuleParameterOverride> parameterValues(const RenderPluginParameterSchema& schema) const;
};

struct RenderPluginFrameInfo {
    std::vector<std::string> passNames;
    std::size_t drawCalls{0};
    bool gpuTimeValid{false};
    bool gpuTimeUpdated{false};
    double gpuMilliseconds{0.0};
};

// Instances are created, rendered and destroyed on the owning GL context
// thread. A plugin owns its private resources; it cannot retain frame borrows.
class RenderPlugin {
public:
    virtual ~RenderPlugin() = default;
    virtual void renderFrame(const RenderPluginFrame& frame) = 0;
    virtual void invalidateHistory() = 0;
    virtual RenderPluginFrameInfo frameInfo() const = 0;
    virtual std::size_t estimatedBytes() const { return 0; }
};

} // namespace iris
