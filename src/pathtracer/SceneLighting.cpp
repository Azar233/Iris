#include "pathtracer/SceneSnapshotCapture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

#include <glm/geometric.hpp>

#include "optics/Atmosphere.h"
#include "render/Renderer.h"

namespace pathtracer {
namespace {

// Equirectangular sky generated for the CPU path tracer. 1024x512 is fine enough that the sun disk
// covers a couple of texels instead of falling between pixel centres -- at 64x32 it misses
// entirely, which is why `generateEquirect` warns about coarse grids -- and it costs tens of
// milliseconds rather than the hundreds a cubemap rebuild takes.
constexpr int skyEquirectWidth = 1024;
constexpr int skyEquirectHeight = 512;

// The GUI re-captures a snapshot for every progressive tile batch and a Batch sequence does it once
// per frame, so the sky is generated on a parameter change rather than per capture. Render Queue
// jobs run on worker threads, hence the lock.
struct SkyCache {
    std::mutex mutex;
    bool valid{false};
    atmosphere::AtmosphereParameters parameters;
    // The cloud layer resolves its parallax against the eye, so the height is part of what the
    // cached image *is* and not just of what produced it: two captures with the same parameters at
    // different heights are different skies and must not share a cache entry.
    float cameraHeight{0.0f};
    std::vector<glm::vec3> pixels;
};

SkyCache& skyCache() {
    static SkyCache cache;
    return cache;
}

// Fills `pixels` from the cache, regenerating it only when the sun or a parameter moved. The copy
// happens under the lock on purpose: `generateEquirect` on another thread would otherwise grow the
// cached buffer while this one is reading it.
void cachedSky(const atmosphere::AtmosphereParameters& parameters, float cameraHeight,
               std::vector<glm::vec3>& pixels) {
    SkyCache& cache = skyCache();
    const std::lock_guard<std::mutex> lock(cache.mutex);
    if (!cache.valid || !atmosphere::parametersMatch(cache.parameters, parameters)
        || std::abs(cache.cameraHeight - cameraHeight) > 0.01f) {
        cache.pixels = atmosphere::generateEquirect(
            parameters, skyEquirectWidth, skyEquirectHeight, cameraHeight
        );
        cache.parameters = parameters;
        cache.cameraHeight = cameraHeight;
        cache.valid = true;
    }
    pixels = cache.pixels;
}

} // namespace

SceneSnapshotLighting captureSceneLighting(const RendererSettings& settings, float cameraHeight) {
    SceneSnapshotLighting lighting;
    const float directionLengthSquared = glm::dot(settings.lightDirection, settings.lightDirection);
    lighting.directional.direction = directionLengthSquared > 1.0e-12f
        ? glm::normalize(settings.lightDirection)
        : glm::vec3(0.0f, -1.0f, 0.0f);
    lighting.directional.radiance = glm::vec3(std::max(settings.diffuseStrength, 0.0f));
    // The traced directional light has to be the same sun the raster path draws: the analytic sky
    // overrides both the direction and the spectrum, and its transmittance luminance carries the
    // same energy scaling `Renderer::render` applies to `diffuseStrength`. Without this the CPU
    // preview of an atmosphere scene would light it with the stale authored white light.
    if (settings.atmosphere.enabled) {
        const glm::vec3 sunDirection = atmosphere::sunDirection(settings.atmosphere);
        lighting.directional.direction = -sunDirection;
        const glm::vec3 transmittance = atmosphere::sunTransmittance(settings.atmosphere);
        const float luminance = 0.2126f * transmittance.r + 0.7152f * transmittance.g
            + 0.0722f * transmittance.b;
        const float sunScale = std::max(luminance, 0.0f)
            * std::max(settings.atmosphere.sunIntensity, 0.0f);
        const float moonScale = atmosphere::moonKeyStrength(settings.atmosphere);
        if (moonScale > sunScale) {
            lighting.directional.direction = -atmosphere::moonDirection(settings.atmosphere);
            lighting.directional.radiance *= moonScale * glm::vec3(0.65f, 0.76f, 1.0f);
        } else {
            lighting.directional.radiance *= sunScale
                * atmosphere::skyLightColor(settings.atmosphere);
        }
    }
    lighting.environment.backgroundColor = settings.backgroundColor;
    lighting.environment.intensity = std::max(settings.environmentIntensity, 0.0f);
    lighting.environment.visibleToCamera = settings.skyboxEnabled;
    lighting.environment.sourceName = settings.iblEnabled
        ? "Active renderer environment"
        : "Background color";
#ifdef MYRENDERER_SOURCE_DIR
    if (settings.iblEnabled && !settings.atmosphere.enabled) {
        lighting.environment.sourcePath = std::filesystem::path(MYRENDERER_SOURCE_DIR)
            / "assets" / "environments"
            / (settings.environmentPreset == 1
                ? "overcast_soil_puresky_2k.exr"
                : "kloofendal_48d_partly_cloudy_puresky_4k.exr");
    }
#endif
    // The analytic sky *is* the environment while it is enabled, exactly as it is on the raster
    // path: the traced frame has to see the same sky, sampled by the same model, or a CPU preview of
    // an outdoor scene would light it with whatever HDR file happened to be bundled. The generated
    // radiance already carries `skyIntensity`, so the environment intensity stays a separate
    // control here just as it is for the raster environment.
    if (settings.atmosphere.enabled) {
        cachedSky(settings.atmosphere, cameraHeight, lighting.environment.radiancePixels);
        lighting.environment.width = static_cast<std::uint32_t>(skyEquirectWidth);
        lighting.environment.height = static_cast<std::uint32_t>(skyEquirectHeight);
        lighting.environment.sourceName = "Analytic atmosphere sky";
        lighting.environment.sourcePath.clear();
    }
    lighting.localLights.reserve(settings.localLights.size());
    for (const LocalLight& source : settings.localLights) {
        SnapshotLocalLight light;
        light.type = source.type == LocalLightType::Spot
            ? SnapshotLocalLightType::Spot
            : SnapshotLocalLightType::Point;
        light.position = source.position;
        const float localDirectionLengthSquared = glm::dot(source.direction, source.direction);
        light.direction = localDirectionLengthSquared > 1.0e-12f
            ? glm::normalize(source.direction)
            : glm::vec3(0.0f, -1.0f, 0.0f);
        light.radiance = source.color * std::max(source.intensity, 0.0f);
        light.radius = std::max(source.radius, 0.0f);
        light.outerConeCosine = source.outerConeCosine;
        lighting.localLights.push_back(light);
    }
    return lighting;
}

} // namespace pathtracer
