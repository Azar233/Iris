#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <glm/mat4x4.hpp>
#include "plugin/RenderPluginContract.h"
#include "module/ParameterRegistry.h"

class Camera;
class Shader;
class RenderTarget;
struct RendererSettings;
struct PostProcessSettings;

namespace iris {
struct RenderPluginParameterSchema;

inline constexpr int renderPluginApiVersion = 2;
inline constexpr const char* enscapePluginId = "iris.enscape-study";
inline constexpr const char* postProcessPluginId = "iris.postprocess";
inline constexpr const char* nprPluginId = "iris.npr";
inline constexpr const char* waterPluginId = "iris.water";
inline constexpr const char* openGlFullscreenService = "opengl.fullscreen.v1";
inline constexpr const char* openGlShadingService = "opengl.material-shading.v1";
inline constexpr const char* openGlIndexedGeometryService = "opengl.indexed-geometry.v1";
inline constexpr const char* openGlShaderBorrowService = "opengl.shader-borrow.v1";
inline constexpr const char* openGlIndexedDrawService = "opengl.indexed-draw.v1";

// Already-resolved backend inputs. No history is owned or advanced here.
struct WaterFrameInputs {
    glm::mat4 currentViewProjection{1.0f};
    glm::mat4 previousViewProjection{1.0f};
    float previousTimeSeconds{0.0f};
    float cameraSurfaceHeight{0.0f};
    bool motionHistoryValid{false};
};

// Borrowed frame inputs. The caller owns the context, target and scheduling.
// RendererSettings is a compatibility bridge for this first migration, not a
// frozen public material/plugin schema or a cross-backend GPU ABI.
struct RenderPluginFrame {
    RenderPluginFrame(RenderTarget& target, const Camera& camera, const RendererSettings& settings,
        int width, int height, float timeSeconds, unsigned int fullscreenVertexArray,
        const PostProcessSettings* postProcessSettings = nullptr, Shader* shadingShader = nullptr,
        const WaterFrameInputs* waterInputs = nullptr);
    RenderTarget& target;
    const Camera& camera;
    const RendererSettings& settings;
    int width;
    int height;
    float timeSeconds;
    unsigned int fullscreenVertexArray;
    const PostProcessSettings* postProcessSettings;
    // Borrowed, already-bound scene shader for MaterialShading extensions.
    // Such extensions only write uniforms; they neither draw nor retain it.
    Shader* shadingShader;
    // Synchronous borrow only. Null preserves the legacy settings-only adapter.
    const WaterFrameInputs* waterInputs;
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
    // CPU-only adaptation of borrowed settings. The backend supplies attachments
    // and debug gating; PostProcess owns execution, resources and history.
    virtual void applyPostProcessSettings(const RendererSettings&, PostProcessSettings&) const {}
    // Context thread only. Allocate/validate private resources without rendering
    // into borrowed targets. A failed candidate is destroyed before publication.
    virtual void prepareResources(const RendererSettings&, int, int) {}
    // Only extensions requiring the indexed-geometry service supply this borrow.
    // The backend owns draw scheduling, attachments and GL draw state.
    virtual IndexedTriangleGeometry borrowIndexedGeometry(const RendererSettings&) const { return {}; }
    // Context-thread borrow for the shader-borrow service. Select the actual
    // backend path; never retain the pointer across replacement/destruction.
    virtual Shader* borrowShadingShader(bool /*deferred*/) const { return nullptr; }
    // Surface/indexed-draw service only, on the owning context thread. The
    // caller binds the shader/uniforms, textures, target, viewport and state.
    // Draw private geometry, leave VAO 0, and return the actual draw count.
    virtual std::size_t drawIndexedGeometry(const RendererSettings&) const;
    virtual void invalidateHistory() = 0;
    virtual RenderPluginFrameInfo frameInfo() const = 0;
    virtual std::size_t estimatedBytes() const { return 0; }
};

} // namespace iris
