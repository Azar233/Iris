#pragma once
#include <filesystem>
#include <memory>
#include <cstddef>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include "optics/Atmosphere.h"
#include "render/CloudNoiseTexture.h"
class Shader;

// One orthographic sun-ray transmission map. All receivers below the cloud base use the same
// integral along a ray, regardless of receiver height. The frame's map is shared by every path.
class CloudShadowRenderer {
public:
    explicit CloudShadowRenderer(const std::filesystem::path& shaderDirectory);
    ~CloudShadowRenderer();
    CloudShadowRenderer(const CloudShadowRenderer&) = delete;
    CloudShadowRenderer& operator=(const CloudShadowRenderer&) = delete;
    void render(const atmosphere::AtmosphereParameters& parameters, const glm::vec3& cameraPosition,
        float extent, float extinction);
    void bind(Shader& shader, unsigned int unit, bool enabled) const;
    unsigned int texture() const { return texture_; }
    int resolution() const { return resolution_; }
    int steps() const { return steps_; }
    bool active() const { return active_; }
    glm::vec3 texelRayOrigin(int x, int y) const;
    std::size_t estimatedBytes() const { return static_cast<std::size_t>(resolution_) * resolution_ * 2U + noiseTexture_.estimatedBytes(); }
private:
    CloudNoiseTexture noiseTexture_;
    std::unique_ptr<Shader> shader_;
    unsigned int framebuffer_{0}, texture_{0}, vertexArray_{0};
    int resolution_{0}, steps_{0};
    float extent_{1};
    glm::vec2 center_{0};
    glm::vec3 axisX_{1,0,0}, axisY_{0,0,1};
    bool active_{false};
    float cloudBase_{0};
};
