#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "asset/ModelData.h"
#include "pathtracer/PbrBsdf.h"
#include "pathtracer/SceneSnapshot.h"

namespace pathtracer {

// Immutable CPU copy of one texture. Color textures are decoded to linear RGB
// before filtering, matching OpenGL sRGB texture sampling.
class CpuTexture {
  public:
    CpuTexture() = default;
    explicit CpuTexture(const TextureData& source);

    bool valid() const { return width_ > 0U && height_ > 0U && !texels_.empty(); }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    const std::vector<glm::vec4>& texels() const { return texels_; }
    glm::vec4 sampleRepeatBilinear(glm::vec2 texCoord) const;

  private:
    std::uint32_t width_{0U};
    std::uint32_t height_{0U};
    std::vector<glm::vec4> texels_;
};

struct EvaluatedPbrMaterial {
    PbrSurface surface;
    glm::vec3 shadingNormal{0.0f, 1.0f, 0.0f};
    float alpha{1.0f};
    float transmission{0.0f};
    float indexOfRefraction{1.5f};
    float thickness{0.0f};
    glm::vec3 attenuationColor{1.0f};
    float attenuationDistance{std::numeric_limits<float>::infinity()};
};

// Per-snapshot decoded texture cache and glTF metallic-roughness material evaluator.
class SceneTextures {
  public:
    explicit SceneTextures(const SceneSnapshot& snapshot);

    EvaluatedPbrMaterial evaluate(
        const SurfaceInteraction& hit,
        const MaterialData& material,
        const glm::vec3& linearTint
    ) const;

  private:
    const CpuTexture* texture(std::uint32_t assetIndex, std::int32_t textureIndex) const;

    std::vector<std::vector<CpuTexture>> assets_;
};

} // namespace pathtracer
