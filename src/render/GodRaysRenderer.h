#pragma once
#include <filesystem>
#include <memory>
#include <cstddef>
#include "optics/Atmosphere.h"
class Camera;
class Shader;
class CloudShadowRenderer;

// Screen-space radial scattering. No history: parameter edits and camera changes take effect
// on the same frame. The context-owning renderer performs allocation, drawing and destruction.
class GodRaysRenderer {
public:
    explicit GodRaysRenderer(const std::filesystem::path& directory);
    ~GodRaysRenderer();
    GodRaysRenderer(const GodRaysRenderer&) = delete;
    GodRaysRenderer& operator=(const GodRaysRenderer&) = delete;
    bool render(const Camera& camera, const atmosphere::AtmosphereParameters& parameters,
        const CloudShadowRenderer& shadow, unsigned int cloudTexture, unsigned int depthTexture,
        int width, int height);
    unsigned int texture() const { return texture_; }
    int bufferWidth() const { return width_; }
    int bufferHeight() const { return height_; }
    std::size_t estimatedBytes() const { return static_cast<std::size_t>(width_) * height_ * 2U; }
private:
    std::unique_ptr<Shader> shader_;
    unsigned int framebuffer_{0}, texture_{0}, vertexArray_{0};
    int width_{0}, height_{0};
};
