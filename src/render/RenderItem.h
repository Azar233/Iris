#pragma once

#include <cstdint>
#include "plugin/ShaderMaterial.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class GpuModel;

struct RenderItem {
    const GpuModel* model{nullptr};
    glm::mat4 modelMatrix{1.0f};
    glm::vec3 tint{1.0f};
    bool visible{true};
    bool castsShadow{true};
    bool instanceCandidate{false};
    std::uint64_t entityId{0U};
    glm::mat4 previousModelMatrix{1.0f};
    bool motionHistoryValid{false};
    iris::ShaderMaterialBinding shaderMaterial{};
};
