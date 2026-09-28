#pragma once

#include "pathtracer/SceneSnapshot.h"

class Camera;
class Scene;
struct RendererSettings;

namespace pathtracer {

SceneSnapshot captureSceneSnapshot(
    const Scene& scene,
    const Camera& camera,
    float aspectRatio,
    SceneSnapshotLighting lighting = {}
);

// `cameraHeight` is the eye's height in world units and feeds the cloud layer's projection, so a
// traced frame places the same cloud in the same direction as the raster frame it is compared with.
SceneSnapshotLighting captureSceneLighting(const RendererSettings& settings, float cameraHeight);

SceneSnapshot captureSceneSnapshot(
    const Scene& scene,
    const Camera& camera,
    float aspectRatio,
    const RendererSettings& settings
);

} // namespace pathtracer
