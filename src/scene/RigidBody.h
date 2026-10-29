#pragma once

#include <cmath>
#include <stdexcept>
#include <glm/vec3.hpp>

// CPU-only authored data. Collision dimensions are in metres, independent of ModelData.
inline constexpr const char* rigidBodyModuleId = "iris.physics.rigid-body";
enum class RigidBodyType { Static = 0, Dynamic = 1 };
enum class CollisionShape { Sphere = 0, Box = 1, Plane = 2 };

struct RigidBodyComponent {
    RigidBodyType type{RigidBodyType::Dynamic};
    CollisionShape shape{CollisionShape::Box};
    glm::vec3 offset{0.0f};
    glm::vec3 halfExtents{0.5f};
    float radius{0.5f};
    float mass{1.0f};
    float friction{0.5f};
    float restitution{0.0f};
    float linearDamping{0.0f};
    float angularDamping{0.05f};
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f}; // radians / second
};

inline bool finitePhysicsVector(const glm::vec3& value, float maximum) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
        && std::abs(value.x) <= maximum && std::abs(value.y) <= maximum && std::abs(value.z) <= maximum;
}

// Shared by Scene codec and runtime: do not silently clamp invalid authored bodies.
inline void validateRigidBody(const RigidBodyComponent& body, const glm::vec3& position,
    const glm::vec3& rotation, const glm::vec3& scale, bool hasParent, bool hasLight) {
    const auto range = [](float value, float minimum, float maximum) {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    };
    if ((body.type != RigidBodyType::Static && body.type != RigidBodyType::Dynamic)
        || (body.shape != CollisionShape::Sphere && body.shape != CollisionShape::Box && body.shape != CollisionShape::Plane)
        || (body.shape == CollisionShape::Plane && body.type != RigidBodyType::Static))
        throw std::runtime_error("Invalid rigid body type/shape; planes must be static");
    if (hasParent || hasLight || scale != glm::vec3(1.0f))
        throw std::runtime_error("Rigid bodies require root entities with unit scale and no light component");
    if (!finitePhysicsVector(position, 1.0e6f) || !finitePhysicsVector(rotation, 1.0e6f)
        || !finitePhysicsVector(body.offset, 1.0e4f) || !finitePhysicsVector(body.halfExtents, 1.0e4f)
        || body.halfExtents.x < 0.001f || body.halfExtents.y < 0.001f || body.halfExtents.z < 0.001f
        || !range(body.radius, 0.001f, 1.0e4f) || !range(body.mass, 0.001f, 1.0e6f)
        || !range(body.friction, 0.0f, 1.0f) || !range(body.restitution, 0.0f, 1.0f)
        || !range(body.linearDamping, 0.0f, 100.0f) || !range(body.angularDamping, 0.0f, 100.0f)
        || !finitePhysicsVector(body.linearVelocity, 1000.0f) || !finitePhysicsVector(body.angularVelocity, 1000.0f))
        throw std::runtime_error("Rigid body has non-finite or out-of-range dimensions/material/motion");
    if (body.type == RigidBodyType::Static
        && (body.linearVelocity != glm::vec3(0.0f) || body.angularVelocity != glm::vec3(0.0f)))
        throw std::runtime_error("Static rigid bodies cannot have initial velocity");
}
