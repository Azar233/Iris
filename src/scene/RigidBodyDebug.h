#pragma once

#include "scene/Scene.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

struct RigidBodyDebugLine { glm::vec3 from, to; };

// Display-only CPU lines; assetMatrix and mesh LOD never change collision geometry.
inline std::vector<RigidBodyDebugLine> rigidBodyDebugLines(const SceneEntity& entity) {
    std::vector<RigidBodyDebugLine> lines;
    if (!entity.rigidBody) return lines;
    const auto& body = *entity.rigidBody;
    const auto transform = glm::translate(glm::mat4(1.0f), entity.transform.translation)
        * glm::mat4_cast(glm::quat(glm::radians(entity.transform.rotationDegrees)));
    const auto world = [&](const glm::vec3& point) { return glm::vec3(transform * glm::vec4(point + body.offset, 1.0f)); };
    const auto line = [&](const glm::vec3& from, const glm::vec3& to) { lines.push_back({world(from), world(to)}); };
    if (body.shape == CollisionShape::Sphere) {
        for (int plane = 0; plane < 3; ++plane) for (int i = 0; i < 32; ++i) {
            glm::vec3 a(0.0f), b(0.0f);
            const float angle = i * 6.283185307f / 32.0f;
            const float next = (i + 1) * 6.283185307f / 32.0f;
            a[(plane + 1) % 3] = std::cos(angle) * body.radius; a[(plane + 2) % 3] = std::sin(angle) * body.radius;
            b[(plane + 1) % 3] = std::cos(next) * body.radius; b[(plane + 2) % 3] = std::sin(next) * body.radius;
            line(a, b);
        }
    } else if (body.shape == CollisionShape::Box) {
        for (int corner = 0; corner < 8; ++corner) for (int axis = 0; axis < 3; ++axis) {
            if (corner & (1 << axis)) continue;
            glm::vec3 a, b;
            for (int j = 0; j < 3; ++j) a[j] = (corner & (1 << j)) ? body.halfExtents[j] : -body.halfExtents[j];
            b = a; b[axis] = -b[axis]; line(a, b);
        }
    } else {
        // An infinite plane is represented by a finite 10 m grid and its +Y normal.
        for (int i = -5; i <= 5; ++i) {
            const float coordinate = static_cast<float>(i);
            line(glm::vec3(-5, 0, coordinate), glm::vec3(5, 0, coordinate));
            line(glm::vec3(coordinate, 0, -5), glm::vec3(coordinate, 0, 5));
        }
        line(glm::vec3(0), glm::vec3(0, 1, 0));
    }
    return lines;
}
