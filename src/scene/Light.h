#pragma once
#include <cmath>
#include <glm/vec3.hpp>

enum class LocalLightType { Point = 0, Spot = 1 };

// Renderer-facing world-space light. Legacy RendererSettings::localLights stays compatible.
struct LocalLight {
    glm::vec3 position{0.0f};
    float radius{3.0f};
    glm::vec3 color{1.0f};
    float intensity{8.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    float outerConeCosine{0.82f};
    LocalLightType type{LocalLightType::Point};
};

// Authoring component: position and direction belong to the entity Transform.
struct SceneLightComponent {
    LocalLightType type{LocalLightType::Point};
    glm::vec3 color{1.0f, 0.86f, 0.66f};
    float intensity{20.0f};
    float range{6.0f};
    float outerAngleDegrees{35.0f}; // Half angle from the local -Y axis.
};

inline bool validSceneLight(const SceneLightComponent& light) {
    return (light.type == LocalLightType::Point || light.type == LocalLightType::Spot)
        && std::isfinite(light.intensity) && light.intensity >= 0.0f && light.intensity <= 10000.0f
        && std::isfinite(light.range) && light.range >= 0.05f && light.range <= 1000.0f
        && std::isfinite(light.outerAngleDegrees) && light.outerAngleDegrees >= 1.0f && light.outerAngleDegrees <= 89.0f
        && std::isfinite(light.color.x) && std::isfinite(light.color.y) && std::isfinite(light.color.z)
        && light.color.x >= 0.0f && light.color.x <= 1.0f
        && light.color.y >= 0.0f && light.color.y <= 1.0f
        && light.color.z >= 0.0f && light.color.z <= 1.0f;
}
