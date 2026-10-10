#include "pathtracer/SceneSnapshotCapture.h"

#include <algorithm>
#include <utility>

#include <glm/trigonometric.hpp>
#include <glm/geometric.hpp>

#include "render/Camera.h"
#include "render/GpuModel.h"
#include "render/Renderer.h"
#include "scene/Scene.h"

namespace pathtracer {

SceneSnapshot captureSceneSnapshot(
    const Scene& scene,
    const Camera& camera,
    float aspectRatio,
    SceneSnapshotLighting lighting
) {
    SnapshotCamera snapshotCamera;
    snapshotCamera.position = camera.position();
    snapshotCamera.view = camera.viewMatrix();
    snapshotCamera.aspectRatio = std::max(aspectRatio, 0.01f);
    snapshotCamera.projection = camera.projectionMatrix(snapshotCamera.aspectRatio);
    snapshotCamera.verticalFieldOfViewRadians = glm::radians(camera.fieldOfView());

    RendererSettings entitySettings;
    entitySettings.localLights = scene.buildLocalLights();
    if (!entitySettings.localLights.empty()) {
        const auto entityLighting = captureSceneLighting(entitySettings, camera.position().y);
        lighting.localLights.insert(lighting.localLights.end(), entityLighting.localLights.begin(), entityLighting.localLights.end());
    }
    SceneSnapshotBuilder builder(snapshotCamera, std::move(lighting));
    for (const SceneEntity& entity : scene.entities()) {
        if (entity.model == nullptr || !entity.visible || !entity.enabledByPreset) continue;
        builder.addModel(
            entity.model->sourceData(),
            entity.id,
            entity.name,
            entity.worldTransform,
            entity.tint,
            entity.castsShadow
        );
    }
    return builder.finish();
}

SceneSnapshot captureSceneSnapshot(
    const Scene& scene,
    const Camera& camera,
    float aspectRatio,
    const RendererSettings& settings
) {
    return captureSceneSnapshot(
        scene,
        camera,
        aspectRatio,
        captureSceneLighting(settings, camera.position().y)
    );
}

} // namespace pathtracer
