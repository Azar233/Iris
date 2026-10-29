#pragma once

#include "scene/Scene.h"

// One transaction used by editor commands and their CPU development checks.
inline bool setEntityRigidBody(Scene& scene, SceneEntityId id,
    const std::optional<RigidBodyComponent>& body, const std::string& moduleId, std::string& error) {
    auto* entity = scene.find(id);
    if (!entity) { error = "Rigid body entity no longer exists"; return false; }
    try {
        if (body) {
            if (moduleId != rigidBodyModuleId) throw std::runtime_error("Select the CPU Rigid Bodies module first");
            validateRigidBody(*body, entity->transform.translation, entity->transform.rotationDegrees,
                entity->transform.scale, entity->parent != invalidSceneEntityId, entity->light.has_value());
        }
        std::size_t total = 0, dynamic = 0;
        for (const auto& candidate : scene.entities()) {
            const auto& value = candidate.id == id ? body : candidate.rigidBody;
            if (value) { ++total; if (value->type == RigidBodyType::Dynamic) ++dynamic; }
        }
        if (total > 128 || dynamic > 16) throw std::runtime_error("Physics supports at most 128 bodies and 16 dynamic bodies");
        entity->rigidBody = body;
        error.clear(); return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}
