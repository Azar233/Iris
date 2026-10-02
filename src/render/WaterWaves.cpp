#include "render/WaterWaves.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace water {
namespace {
constexpr float pi = 3.14159265358979323846f;
}

void applyPreset(WaterSettings& settings, WaterPreset preset) {
    settings.preset = preset;
    switch (preset) {
    case WaterPreset::Custom: break;
    case WaterPreset::Calm:
        settings.amplitude = 0.08f;
        settings.speed = 0.55f;
        settings.steepness = 0.30f;
        settings.foamStrength = 0.20f;
        settings.windDirection = glm::vec2(1.0f, 0.0f);
        break;
    case WaterPreset::Windy:
        settings.amplitude = 0.30f;
        settings.speed = 1.25f;
        settings.steepness = 0.70f;
        settings.foamStrength = 0.65f;
        settings.windDirection = glm::vec2(0.9f, 0.3f);
        break;
    case WaterPreset::Storm:
        settings.amplitude = 0.65f;
        settings.speed = 2.0f;
        settings.steepness = 0.85f;
        settings.foamStrength = 1.0f;
        settings.windDirection = glm::vec2(0.8f, 0.6f);
        break;
    }
}

int activeComponentCount(const WaterSettings& settings) {
    if (settings.quality == WaterQuality::Low) return 2;
    return settings.waveDiversity > 0.0f ? componentCount : 4;
}

std::array<glm::vec4, componentCount> components(const WaterSettings& settings) {
    glm::vec2 wind = settings.windDirection;
    if (glm::dot(wind, wind) < 1.0e-6f) wind = glm::vec2(1.0f, 0.0f);
    wind = glm::normalize(wind);
    const glm::vec2 crossWind(-wind.y, wind.x);
    const float diversity = std::clamp(settings.waveDiversity, 0.0f, 1.0f);
    const float legacyScale = 1.0f - diversity * 0.35f;
    return {{
        glm::vec4(wind, settings.amplitude * legacyScale, 15.0f),
        glm::vec4(glm::normalize(wind * 0.86f + crossWind * 0.51f), settings.amplitude * 0.55f * legacyScale, 7.5f),
        glm::vec4(glm::normalize(wind * 0.71f - crossWind * 0.70f), settings.amplitude * 0.28f * legacyScale, 3.4f),
        glm::vec4(glm::normalize(wind * 0.54f + crossWind * 0.84f), settings.amplitude * 0.12f * legacyScale, 1.6f),
        glm::vec4(glm::normalize(wind * 0.96f - crossWind * 0.28f), settings.amplitude * 0.35f * diversity, 21.7f),
        glm::vec4(glm::normalize(wind * 0.77f + crossWind * 0.64f), settings.amplitude * 0.28f * diversity, 11.2f),
        glm::vec4(glm::normalize(wind * 0.88f - crossWind * 0.47f), settings.amplitude * 0.20f * diversity, 5.4f),
        glm::vec4(glm::normalize(wind * 0.64f + crossWind * 0.77f), settings.amplitude * 0.10f * diversity, 2.5f)
    }};
}

WaterSample evaluate(const WaterSettings& settings, const glm::vec2& position) {
    WaterSample sample;
    sample.position = glm::vec3(position.x, settings.level, position.y);
    glm::vec3 tangentX(1.0f, 0.0f, 0.0f);
    glm::vec3 tangentZ(0.0f, 0.0f, 1.0f);
    const float diversity = std::clamp(settings.waveDiversity, 0.0f, 1.0f);
    const float warpA = 0.037f * position.x + 0.071f * position.y;
    const float warpB = 0.091f * position.x - 0.026f * position.y;
    const float warpC = -0.055f * position.x + 0.041f * position.y;
    const float warpD = 0.024f * position.x + 0.087f * position.y;
    const glm::vec2 warp = diversity * glm::vec2(
        2.8f * std::sin(warpA) + 1.3f * std::sin(warpB),
        2.1f * std::sin(warpC) + 1.1f * std::sin(warpD));
    const glm::vec2 warpDx = diversity * glm::vec2(
        2.8f * 0.037f * std::cos(warpA) + 1.3f * 0.091f * std::cos(warpB),
        -2.1f * 0.055f * std::cos(warpC) + 1.1f * 0.024f * std::cos(warpD));
    const glm::vec2 warpDz = diversity * glm::vec2(
        2.8f * 0.071f * std::cos(warpA) - 1.3f * 0.026f * std::cos(warpB),
        2.1f * 0.041f * std::cos(warpC) + 1.1f * 0.087f * std::cos(warpD));
    const auto waves = components(settings);
    for (int index = 0; index < activeComponentCount(settings); ++index) {
        const glm::vec4& wave = waves[static_cast<std::size_t>(index)];
        const glm::vec2 direction(wave.x, wave.y);
        const float amplitude = wave.z;
        const float k = 2.0f * pi / wave.w;
        const float phaseSpeed = std::sqrt(9.81f / k) * settings.speed;
        const float phase = k * (glm::dot(direction, position + warp)
            - phaseSpeed * settings.timeSeconds)
            + (index >= 4 ? static_cast<float>(index - 3) * 1.731f : 0.0f);
        const float phaseDx = k * (direction.x + glm::dot(direction, warpDx));
        const float phaseDz = k * (direction.y + glm::dot(direction, warpDz));
        const float sine = std::sin(phase);
        const float cosine = std::cos(phase);
        const float horizontal = settings.steepness * amplitude
            / (k * std::max(waves[0].z, 1.0e-4f) * 4.0f);
        sample.position.x += horizontal * direction.x * cosine;
        sample.position.y += amplitude * sine;
        sample.position.z += horizontal * direction.y * cosine;
        tangentX += glm::vec3(-horizontal * direction.x * phaseDx * sine,
                              amplitude * phaseDx * cosine,
                              -horizontal * direction.y * phaseDx * sine);
        tangentZ += glm::vec3(-horizontal * direction.x * phaseDz * sine,
                              amplitude * phaseDz * cosine,
                              -horizontal * direction.y * phaseDz * sine);
        sample.velocity += glm::vec3(
            horizontal * direction.x * k * phaseSpeed * sine,
            -amplitude * k * phaseSpeed * cosine,
            horizontal * direction.y * k * phaseSpeed * sine);
    }
    sample.normal = glm::normalize(glm::cross(tangentZ, tangentX));
    sample.tangent = glm::normalize(tangentX);
    return sample;
}

float gridCoordinate(float logicalCoordinate, float extent, float nearMeshFocus) {
    const float coordinate = std::clamp(logicalCoordinate, -1.0f, 1.0f);
    const float safeExtent = std::max(extent, 0.0f);
    const float radius = std::abs(coordinate);
    const float quadratic = radius * radius * safeExtent;
    constexpr float nearScale = 0.6f;
    const float focused = nearScale
        * std::expm1(std::log1p(safeExtent / nearScale) * radius);
    const float focus = std::clamp(nearMeshFocus, 0.0f, 1.0f);
    return std::copysign(quadratic + (focused - quadratic) * focus, coordinate);
}

} // namespace water
