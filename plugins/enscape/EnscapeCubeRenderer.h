#pragma once

#include <array>
#include <filesystem>
#include <memory>

#include <glm/mat4x4.hpp>
#include "plugin/RenderPlugin.h"

class Camera;
class RenderTarget;
class Shader;
struct RendererSettings;
struct EnscapeCubeSettings;

// Isolated four-pass Shadertoy study. It owns only GL resources; the scene
// document remains the switch and the ordinary renderer handles other scenes.
class EnscapeCubeRenderer final : public iris::RenderPlugin {
public:
    explicit EnscapeCubeRenderer(const std::filesystem::path& shaderDirectory);
    ~EnscapeCubeRenderer() override;
    EnscapeCubeRenderer(const EnscapeCubeRenderer&) = delete;
    EnscapeCubeRenderer& operator=(const EnscapeCubeRenderer&) = delete;

    void render(RenderTarget& target, const Camera& camera,
        const RendererSettings& settings, int width, int height, float timeSeconds,
        unsigned int fullscreenVertexArray);
    void renderFrame(const iris::RenderPluginFrame& frame) override;
    iris::RenderPluginFrameInfo frameInfo() const override;
    void invalidateHistory() override { historyValid_ = false; }
    bool hasGpuFrameTime() const { return gpuTimeValid_; }
    bool gpuFrameTimeUpdated() const { return gpuTimeUpdated_; }
    double gpuFrameMilliseconds() const { return gpuTimeMilliseconds_; }

private:
    void resize(int width, int height);
    void makeNoiseTextures();
    void draw(Shader& shader, unsigned int framebuffer, int width, int height,
        float timeSeconds, unsigned int fullscreenVertexArray,
        const Camera& camera, const EnscapeCubeSettings& parameters);

    std::array<std::unique_ptr<Shader>, 4> shaders_;
    std::array<unsigned int, 4> textures_{};
    std::array<unsigned int, 4> framebuffers_{};
    unsigned int weatherTexture_{0};
    unsigned int volumeTexture_{0};
    int width_{0};
    int height_{0};
    unsigned int historyIndex_{0};
    std::array<unsigned int, 3> timingQueries_{};
    std::array<bool, 3> timingPending_{};
    bool gpuTimeValid_{false};
    bool gpuTimeUpdated_{false};
    double gpuTimeMilliseconds_{0.0};
    bool historyValid_{false};
    bool cameraValid_{false};
    bool parametersValid_{false};
    glm::mat4 previousViewProjection_{1.0f};
    std::array<float, 13> previousParameters_{};
    float previousTime_{0.0f};
};
