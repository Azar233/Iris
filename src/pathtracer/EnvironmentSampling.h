#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "pathtracer/SceneSnapshot.h"

namespace pathtracer {

struct EnvironmentSample {
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    glm::vec3 radiance{0.0f};
    float pdf{0.0f};
    bool valid{false};
};

// Equirectangular, linear-radiance environment with a luminance*sin(theta)
// distribution. Constant backgrounds remain lookup-only and are not sampled.
class EnvironmentLight {
  public:
    EnvironmentLight() = default;
    explicit EnvironmentLight(const SnapshotEnvironment& environment);

    bool importanceSampled() const { return !cdf_.empty() && totalWeight_ > 0.0; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    const std::vector<double>& cumulativeWeights() const { return cdf_; }
    glm::vec3 radiance(const glm::vec3& direction) const;
    EnvironmentSample sample(const glm::vec2& sample) const;
    float pdf(const glm::vec3& direction) const;

  private:
    glm::vec3 texel(int x, int y) const;

    glm::vec3 backgroundColor_{0.0f};
    float intensity_{1.0f};
    std::uint32_t width_{0U};
    std::uint32_t height_{0U};
    std::vector<glm::vec3> pixels_;
    std::vector<double> weights_;
    std::vector<double> cdf_;
    double totalWeight_{0.0};
};

} // namespace pathtracer
