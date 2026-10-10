#include "module/RuntimeScene.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t fnvOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

std::uint64_t mixBytes(std::uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= fnvPrime;
    }
    return hash;
}

std::uint64_t mixUint64(std::uint64_t hash, std::uint64_t value) {
    return mixBytes(hash, &value, sizeof(value));
}

std::uint64_t mixFloat(std::uint64_t hash, float value) {
    // Normalise -0.0 so a signed zero cannot look like different content.
    const float normalised = value == 0.0f ? 0.0f : value;
    return mixBytes(hash, &normalised, sizeof(normalised));
}

std::uint64_t mixVector(std::uint64_t hash, const glm::vec3& value) {
    hash = mixFloat(hash, value.x);
    hash = mixFloat(hash, value.y);
    return mixFloat(hash, value.z);
}

} // namespace

std::uint64_t RuntimeScene::resetFrom(const Scene& editScene) {
    scene_.clear();
    for (const SceneEntity& source : editScene.entities()) {
        scene_.createEntityWithId(source.id, source.name, source.model, source.modelResource);
    }
    // Second pass: parents may be declared after their children, so hierarchy is
    // copied only once every id exists. This is a faithful copy, not a user edit, so
    // it deliberately bypasses parenting validation.
    for (const SceneEntity& source : editScene.entities()) {
        SceneEntity* target = scene_.find(source.id);
        if (target == nullptr) continue;
        target->parent = source.parent;
        target->transform = source.transform;
        target->worldTransform = source.worldTransform;
        target->previousWorldTransform = source.worldTransform;
        target->tint = source.tint;
        target->light = source.light;
        target->visible = source.visible;
        target->castsShadow = source.castsShadow;
        target->instanceCandidate = source.instanceCandidate;
        target->enabledByPreset = source.enabledByPreset;
        target->motionHistoryValid = false;
        target->worldTransformInitialized = source.worldTransformInitialized;
    }
    ++generation_;
    dirty_ = false;
    return generation_;
}

std::uint64_t RuntimeScene::contentHash() const {
    return sceneContentHash(scene_);
}

std::uint64_t sceneContentHash(const Scene& scene) {
    std::vector<const SceneEntity*> ordered;
    ordered.reserve(scene.entities().size());
    for (const SceneEntity& entity : scene.entities()) ordered.push_back(&entity);
    std::sort(ordered.begin(), ordered.end(), [](const SceneEntity* left, const SceneEntity* right) {
        return left->id < right->id;
    });

    std::uint64_t hash = fnvOffsetBasis;
    for (const SceneEntity* entity : ordered) {
        hash = mixUint64(hash, entity->id);
        hash = mixUint64(hash, entity->parent);
        hash = mixBytes(hash, entity->name.data(), entity->name.size());
        // The resource path is the stable asset identity. The model pointer is
        // deliberately not hashed: it changes between runs, which would defeat any
        // cross-run cache reuse. Procedurally created models share an empty resource
        // string, so callers pair this hash with the scene generation.
        hash = mixBytes(hash, entity->modelResource.data(), entity->modelResource.size());
        hash = mixVector(hash, entity->transform.translation);
        hash = mixVector(hash, entity->transform.rotationDegrees);
        hash = mixVector(hash, entity->transform.scale);
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                hash = mixFloat(hash, entity->transform.assetTransform[column][row]);
            }
        }
        hash = mixVector(hash, entity->tint);
        hash = mixUint64(hash, entity->visible ? 1U : 0U);
        hash = mixUint64(hash, entity->castsShadow ? 1U : 0U);
        hash = mixUint64(hash, entity->instanceCandidate ? 1U : 0U);
        // Preserve old hashes for scenes without this additive component.
        if (entity->light) {
            const auto& light = *entity->light;
            hash = mixUint64(hash, 0x4C49474854ULL);
            hash = mixUint64(hash, static_cast<std::uint64_t>(light.type));
            hash = mixVector(hash, light.color); hash = mixFloat(hash, light.intensity);
            hash = mixFloat(hash, light.range); hash = mixFloat(hash, light.outerAngleDegrees);
        }
    }
    return hash;
}
