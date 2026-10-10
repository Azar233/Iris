#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include "scene/Light.h"
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "render/RenderItem.h"

class GpuModel;

using SceneEntityId = std::uint64_t;
inline constexpr SceneEntityId invalidSceneEntityId = 0U;

struct SceneTransform {
    glm::vec3 translation{0.0f};
    glm::vec3 rotationDegrees{0.0f};
    glm::vec3 scale{1.0f};
    glm::mat4 assetTransform{1.0f};

    glm::mat4 matrix() const;
};

struct SceneEntity {
    SceneEntityId id{invalidSceneEntityId};
    std::string name{"Entity"};
    SceneEntityId parent{invalidSceneEntityId};
    const GpuModel* model{nullptr};
    std::string modelResource;
    SceneTransform transform;
    glm::mat4 worldTransform{1.0f};
    glm::mat4 previousWorldTransform{1.0f};
    glm::vec3 tint{1.0f};
    std::optional<SceneLightComponent> light;
    bool visible{true};
    bool enabledByPreset{true};
    bool castsShadow{true};
    bool instanceCandidate{false};
    bool motionHistoryValid{false};
    bool worldTransformInitialized{false};
};

class Scene {
public:
    SceneEntityId createEntity(
        std::string name,
        const GpuModel* model = nullptr,
        std::string modelResource = {}
    );
    SceneEntityId createEntityWithId(
        SceneEntityId id,
        std::string name,
        const GpuModel* model = nullptr,
        std::string modelResource = {}
    );
    SceneEntityId createLightEntity(std::string name, const SceneLightComponent& light);
    std::size_t lightEntityCount() const;
    std::vector<LocalLight> buildLocalLights() const;
    SceneEntityId duplicateEntity(SceneEntityId source);
    bool destroyEntity(SceneEntityId id);
    bool setParent(SceneEntityId child, SceneEntityId parent);
    void clear();

    SceneEntity* find(SceneEntityId id);
    const SceneEntity* find(SceneEntityId id) const;
    const std::vector<SceneEntity>& entities() const { return entities_; }
    std::size_t size() const { return entities_.size(); }

    void beginFrame();
    void updateWorldTransforms();
    std::vector<RenderItem> buildRenderItems() const;

private:
    bool isDescendant(SceneEntityId ancestor, SceneEntityId candidate) const;
    glm::mat4 resolveWorldTransform(
        SceneEntityId id,
        std::vector<SceneEntityId>& resolving
    );

    std::vector<SceneEntity> entities_;
    SceneEntityId nextId_{1U};
};
