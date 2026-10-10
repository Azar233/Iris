#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <optional>
#include <vector>

#include "optics/PrismDemo.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "scene/Scene.h"
#include "module/ParameterRegistry.h"

inline constexpr const char* myRendererSceneExtension = ".myscene";
inline constexpr const char* builtinGroundResource = "builtin:ground-plane";
inline constexpr const char* builtinGlassBackdropResource = "builtin:glass-checkerboard";

struct SceneDocumentEntity {
    SceneEntityId id{invalidSceneEntityId};
    std::string name{"Entity"};
    SceneEntityId parent{invalidSceneEntityId};
    std::string modelResource;
    SceneTransform transform;
    glm::vec3 tint{1.0f};
    std::optional<SceneLightComponent> light;
    bool visible{true};
    bool castsShadow{true};
    bool instanceCandidate{false};
};

struct ScenePlaybackSettings {
    bool animationEnabled{false};
    bool animationPlaying{true};
    float animationTimeSeconds{0.0f};
    float animationSpeed{1.0f};
    std::size_t animationClipIndex{0U};
    bool prismEnabled{false};
    bool prismCameraLocked{false};
    PrismOpticalPreset prismPreset{PrismOpticalPreset::CrownGlass};
    PrismDemoParameters prismParameters{};
};

struct SceneDocument {
    static constexpr int currentVersion = 2;

    CameraOrbitState camera;
    RendererSettings renderer;
    ScenePlaybackSettings playback;
    std::string moduleId;
    std::uint32_t moduleSeed{20260919U};
    std::vector<ModuleParameterOverride> moduleParameters;
    std::vector<SceneDocumentEntity> entities;
};

bool saveSceneDocument(
    const std::filesystem::path& path,
    const SceneDocument& document,
    std::string& error
);

bool loadSceneDocument(
    const std::filesystem::path& path,
    SceneDocument& document,
    std::string& error
);

std::string makeSceneRelativeResource(
    const std::string& resource,
    const std::filesystem::path& scenePath
);

std::filesystem::path resolveSceneResource(
    const std::string& resource,
    const std::filesystem::path& scenePath
);

// Shared logical Scene resolution for CPU batch lighting (same transforms as the editor).
inline std::vector<LocalLight> resolveSceneLocalLights(const SceneDocument& document) {
    Scene scene;
    for (const auto& saved : document.entities) {
        const auto id = scene.createEntityWithId(saved.id, saved.name);
        auto* entity = scene.find(id);
        if (!entity) continue;
        entity->parent = saved.parent; entity->transform = saved.transform;
        entity->light = saved.light; entity->visible = saved.visible;
    }
    scene.updateWorldTransforms();
    auto lights = document.renderer.localLights;
    const auto entities = scene.buildLocalLights();
    lights.insert(lights.end(), entities.begin(), entities.end());
    return lights;
}
