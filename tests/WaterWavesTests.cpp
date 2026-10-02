#include <cmath>
#include <cstdlib>
#include <iostream>

#include <glm/geometric.hpp>

#include "render/WaterWaves.h"

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    WaterSettings settings;
    water::applyPreset(settings, WaterPreset::Calm);
    require(settings.preset == WaterPreset::Calm && settings.amplitude < 0.1f,
        "calm preset must lower wave amplitude");
    const float calmAmplitude = settings.amplitude;
    water::applyPreset(settings, WaterPreset::Windy);
    const float windyAmplitude = settings.amplitude;
    water::applyPreset(settings, WaterPreset::Storm);
    require(settings.preset == WaterPreset::Storm
        && calmAmplitude < windyAmplitude && windyAmplitude < settings.amplitude
        && settings.foamStrength > 0.9f,
        "sea-state presets must increase waves and whitecaps");
    settings = WaterSettings{};
    settings.enabled = true;
    settings.timeSeconds = 1.25f;
    const glm::vec2 point(4.0f, -7.0f);
    const WaterSample sample = water::evaluate(settings, point);
    require(water::activeComponentCount(settings) == 4,
        "high quality must evaluate four wave components");
    require(std::isfinite(sample.position.x) && std::isfinite(sample.normal.y)
        && std::isfinite(sample.velocity.z), "water outputs must be finite");
    require(std::abs(glm::length(sample.normal) - 1.0f) < 1.0e-5f,
        "analytic normal must be normalized");
    require(std::abs(glm::dot(sample.normal, sample.tangent)) < 1.0e-4f,
        "analytic normal and tangent must be orthogonal");

    constexpr float step = 1.0e-3f;
    settings.timeSeconds += step;
    const WaterSample later = water::evaluate(settings, point);
    settings.timeSeconds -= 2.0f * step;
    const WaterSample earlier = water::evaluate(settings, point);
    const glm::vec3 numericalVelocity = (later.position - earlier.position) / (2.0f * step);
    require(glm::length(numericalVelocity - sample.velocity) < 0.003f,
        "analytic velocity must match time differentiation");

    settings.timeSeconds += step;
    settings.quality = WaterQuality::Low;
    require(water::activeComponentCount(settings) == 2,
        "low quality must evaluate two wave components");
    const WaterSample lowSample = water::evaluate(settings, point);
    require(glm::length(lowSample.position - sample.position) > 1.0e-4f,
        "quality tier must change fine-scale wave synthesis");

    settings.quality = WaterQuality::High;
    settings.waveDiversity = 1.0f;
    const WaterSample diverseSample = water::evaluate(settings, point);
    require(water::activeComponentCount(settings) == 8
        && glm::length(diverseSample.position - sample.position) > 1.0e-4f,
        "wave diversity must add distinct geometric components");
    const glm::vec3 numericalX = (water::evaluate(settings, point + glm::vec2(step, 0.0f)).position
        - water::evaluate(settings, point - glm::vec2(step, 0.0f)).position) / (2.0f * step);
    const glm::vec3 numericalZ = (water::evaluate(settings, point + glm::vec2(0.0f, step)).position
        - water::evaluate(settings, point - glm::vec2(0.0f, step)).position) / (2.0f * step);
    require(glm::length(glm::normalize(glm::cross(numericalZ, numericalX))
        - diverseSample.normal) < 0.01f,
        "warped-wave analytic normal must match spatial differentiation");
    settings.timeSeconds += step;
    const WaterSample diverseLater = water::evaluate(settings, point);
    settings.timeSeconds -= 2.0f * step;
    const WaterSample diverseEarlier = water::evaluate(settings, point);
    const glm::vec3 diverseVelocity = (diverseLater.position - diverseEarlier.position)
        / (2.0f * step);
    require(glm::length(diverseVelocity - diverseSample.velocity) < 0.003f,
        "diverse waves must preserve analytic motion velocity");

    settings.amplitude = 0.0f;
    const WaterSample flat = water::evaluate(settings, point);
    require(glm::length(flat.position - glm::vec3(point.x, settings.level, point.y)) < 1.0e-6f
        && glm::length(flat.normal - glm::vec3(0.0f, 1.0f, 0.0f)) < 1.0e-6f,
        "zero amplitude must recover a flat plane");

    const float extent = 120.0f;
    const float logicalStep = 2.0f / water::gridResolution;
    const float nearSpacing = water::gridCoordinate(logicalStep, extent)
        - water::gridCoordinate(0.0f, extent);
    const float farSpacing = water::gridCoordinate(1.0f, extent)
        - water::gridCoordinate(1.0f - logicalStep, extent);
    require(nearSpacing > 0.0f && farSpacing > nearSpacing * 100.0f,
        "camera-centered grid must devote more vertices to nearby water");
    const float lowNearSpacing = water::gridCoordinate(
        2.0f / water::lowGridResolution, extent);
    require(lowNearSpacing > nearSpacing * 3.9f
        && water::gridResolution / water::lowGridResolution == 2,
        "low quality grid must halve per-axis resolution");
    const float openOceanExtent = 2000.0f;
    const float nearDistance = 5.0f;
    const float oldLogical = std::sqrt(nearDistance / openOceanExtent);
    const float focusedLogical = std::log1p(nearDistance / 0.6f)
        / std::log1p(openOceanExtent / 0.6f);
    const float oldCell = water::gridCoordinate(oldLogical + logicalStep, openOceanExtent)
        - water::gridCoordinate(oldLogical, openOceanExtent);
    const float focusedCell = water::gridCoordinate(
        focusedLogical + logicalStep, openOceanExtent, 1.0f)
        - water::gridCoordinate(focusedLogical, openOceanExtent, 1.0f);
    require(focusedCell < oldCell * 0.35f && focusedCell < 2.5f / 4.0f,
        "focused open-ocean grid must resolve near-camera short geometry waves");
    require(std::abs(water::gridCoordinate(1.0f, openOceanExtent, 1.0f)
        - openOceanExtent) < 0.001f
        && std::abs(water::gridCoordinate(-1.0f, openOceanExtent, 1.0f)
            + openOceanExtent) < 0.001f,
        "focused grid must remain continuous across the camera and reach the horizon");
    std::cout << "Water wave synthesis and camera grid: PASS\n";
}
