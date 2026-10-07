#pragma once

#include <filesystem>
#include <string>
#include <vector>

class Camera;
class RenderTarget;
struct RendererSettings;

namespace iris {

inline constexpr int renderPluginApiVersion = 1;
inline constexpr const char* enscapePluginId = "iris.enscape-study";
inline constexpr const char* openGlFullscreenService = "opengl.fullscreen.v1";

// Borrowed frame inputs. The caller owns the context, target and scheduling.
// RendererSettings is a compatibility bridge for this first migration, not a
// frozen public material/plugin schema or a cross-backend GPU ABI.
struct RenderPluginFrame {
    RenderTarget& target;
    const Camera& camera;
    const RendererSettings& settings;
    int width;
    int height;
    float timeSeconds;
    unsigned int fullscreenVertexArray;
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
};

} // namespace iris
