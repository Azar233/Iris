#pragma once

#include <array>
#include <filesystem>
#include <memory>

class RenderTarget;
class Shader;

// Isolated four-pass Shadertoy study. It owns only GL resources; the scene
// document remains the switch and the ordinary renderer handles other scenes.
class EnscapeCubeRenderer {
public:
    explicit EnscapeCubeRenderer(const std::filesystem::path& shaderDirectory);
    ~EnscapeCubeRenderer();
    EnscapeCubeRenderer(const EnscapeCubeRenderer&) = delete;
    EnscapeCubeRenderer& operator=(const EnscapeCubeRenderer&) = delete;

    void render(RenderTarget& target, int width, int height, float timeSeconds,
        unsigned int fullscreenVertexArray);
    void invalidateHistory() { historyValid_ = false; }

private:
    void resize(int width, int height);
    void makeNoiseTextures();
    void draw(Shader& shader, unsigned int framebuffer, int width, int height,
        float timeSeconds, unsigned int fullscreenVertexArray);

    std::array<std::unique_ptr<Shader>, 4> shaders_;
    std::array<unsigned int, 4> textures_{};
    std::array<unsigned int, 4> framebuffers_{};
    unsigned int weatherTexture_{0};
    unsigned int volumeTexture_{0};
    int width_{0};
    int height_{0};
    unsigned int historyIndex_{0};
    bool historyValid_{false};
    float previousTime_{0.0f};
};
