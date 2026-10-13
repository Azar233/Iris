#pragma once
#include <glm/vec4.hpp>
#include <cmath>

namespace iris {
// Runtime-only: normalized bottom-left xy, buttons (L=1/R=2/M=4), inside flag.
// Hidden viewport, UI capture and offline output all use exactly zero.
inline glm::vec4 shaderSceneMouseInput(float x, float y, float left, float top,
    float width, float height, int buttons, bool accepted) {
    if (!accepted || width <= 0 || height <= 0 || !std::isfinite(x) || !std::isfinite(y)
        || x < left || x >= left + width || y < top || y >= top + height) return glm::vec4(0);
    return {(x-left)/width, 1.f-(y-top)/height, static_cast<float>(buttons & 7), 1.f};
}
inline bool validShaderSceneMouseInput(const glm::vec4& input) {
    if (input.w == 0) return input == glm::vec4(0);
    return input.w == 1 && std::isfinite(input.x) && std::isfinite(input.y)
        && input.x >= 0 && input.x <= 1 && input.y >= 0 && input.y <= 1
        && std::isfinite(input.z) && input.z >= 0 && input.z <= 7 && std::floor(input.z) == input.z;
}
}
