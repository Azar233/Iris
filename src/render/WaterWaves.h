#pragma once

#include <array>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// Wave Synthesis, not a fluid solver. The four components are uploaded unchanged to the GPU.
enum class WaterPreset : int { Custom = 0, Calm = 1, Windy = 2, Storm = 3 };
enum class WaterQuality : int { Low = 0, High = 1 };

struct WaterSettings {
    bool enabled{false};
    WaterPreset preset{WaterPreset::Custom};
    WaterQuality quality{WaterQuality::High};
    float level{-0.45f};
    float extent{110.0f};
    float amplitude{0.22f};
    float speed{1.0f};
    float steepness{0.65f};
    float foamStrength{0.7f};
    float roughness{0.25f};
    float reflectionStrength{1.0f};
    float rippleStrength{0.0f};
    float sunGlintStrength{1.0f};
    glm::vec2 windDirection{0.9f, 0.3f};
    float timeSeconds{0.0f};
};

struct WaterSample {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 tangent{1.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
};

namespace water {

inline constexpr int componentCount = 4;
inline constexpr int gridResolution = 192;
inline constexpr int lowGridResolution = 96;

void applyPreset(WaterSettings& settings, WaterPreset preset);
int activeComponentCount(const WaterSettings& settings);
std::array<glm::vec4, componentCount> components(const WaterSettings& settings);
WaterSample evaluate(const WaterSettings& settings, const glm::vec2& position);
float gridCoordinate(float logicalCoordinate, float extent);

} // namespace water
