#include "optics/CloudReference.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "optics/CloudParams.h"
#include "optics/CloudLightingLut.h"

namespace cloud {
namespace {

SlabSpan clippedViewSpan(
    float originY,
    float directionY,
    const atmosphere::AtmosphereParameters& parameters
) {
    SlabSpan span = slabSpan(originY, directionY, parameters.cloudBaseHeight,
        parameters.cloudTopHeight);
    if (!span.valid) return span;
    const float maximumDistance = maximumViewDistance(parameters);
    if (span.start >= maximumDistance) return {};
    span.length = std::min(span.length, maximumDistance - span.start);
    span.valid = span.length > 0.0f;
    return span;
}

float horizonBlend(float directionY, float fadeDegrees) {
    if (fadeDegrees <= 0.0f) return 1.0f;
    const float edge = std::sin(glm::radians(std::clamp(fadeDegrees, 0.0f, 30.0f)));
    const float t = std::clamp(directionY / std::max(edge, 1.0e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Optical depth from `position` towards the sun, integrated by marching to the top of the slab.
// The march stops at the slab's exit point rather than at a fixed distance, so a sample near the
// top of the layer does not pay for empty air above it.
float sunOpticalDepth(
    const glm::vec3& position,
    const glm::vec3& sunDirection,
    const atmosphere::AtmosphereParameters& parameters,
    const MarchSettings& settings,
    int& sampleCounter
) {
    const SlabSpan span = slabSpan(position.y, sunDirection.y, parameters.cloudBaseHeight,
        parameters.cloudTopHeight);
    if (!span.valid || span.length <= 0.0f) return 0.0f;
    const int steps = std::max(settings.lightSteps, 1);
    // Centred sampling: each step reads the middle of its own interval, which is the same rule the
    // primary march uses and keeps a single sample from landing exactly on a slab boundary.
    const float stepLength = span.length / static_cast<float>(steps);
    float depth = 0.0f;
    for (int step = 0; step < steps; ++step) {
        const float nearFraction = myrenderer_cloud_light_fraction(
            static_cast<float>(step) / steps, parameters.cloudHeightLighting);
        const float farFraction = myrenderer_cloud_light_fraction(
            static_cast<float>(step + 1) / steps, parameters.cloudHeightLighting);
        const float intervalLength = span.length * (farFraction - nearFraction);
        const float distance = parameters.cloudHeightLighting
            ? span.start + span.length * (nearFraction + farFraction) * 0.5f
            : span.start + (static_cast<float>(step) + 0.5f) * stepLength;
        const glm::vec3 sample = position + sunDirection * distance;
        depth += densityAt(sample, parameters)
            * (parameters.cloudHeightLighting ? intervalLength : stepLength);
        ++sampleCounter;
    }
    return depth;
}

} // namespace

MarchLighting marchLighting(const atmosphere::AtmosphereParameters& parameters) {
    MarchLighting lighting;
    // The sky above the layer, scaled by the march's own coefficient. An integrating march sums
    // dozens of weighted samples where the analytic layer took one, so the same radiance is far
    // brighter here; the two scales are separate parameters for exactly that reason.
    lighting.ambient = atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), parameters)
        * std::max(parameters.cloudVolumetricAmbientScale, 0.0f);
    // The key light's spectrum carrying its intensity. The march applies the phase function and the
    // light march itself, so this is the unmodulated source.
    lighting.sun = (parameters.cloudHeightLighting
            ? atmosphere::sunTransmittance(parameters) : atmosphere::skyLightColor(parameters))
        * std::max(parameters.sunIntensity, 0.0f)
        * std::max(parameters.cloudVolumetricSunScale, 0.0f);
    if (parameters.cloudHeightLighting) {
        // The flat ground blocks a source below the horizon. Preserve the solar
        // spectrum AND its energy instead of normalizing sunset extinction away.
        const float visible = std::clamp(parameters.sunElevationDegrees / 2.0f, 0.0f, 1.0f);
        lighting.sun *= visible * visible * (3.0f - 2.0f * visible);
    }
    return lighting;
}

float maximumViewDistance(const atmosphere::AtmosphereParameters& parameters) {
    const float period = std::clamp(std::round(parameters.cloudNoisePeriod), 1.0f, 16.0f);
    const float shapeReach = std::max(parameters.cloudFeatureScale, 1.0f) * period * 2.0f;
    const float weatherReach = std::max(parameters.cloudWeatherScale, 1.0f) * 2.0f;
    return std::clamp(std::max(shapeReach, weatherReach), 10000.0f, 200000.0f);
}

float densityAt(const glm::vec3& position, const atmosphere::AtmosphereParameters& parameters) {
    return myrenderer_cloud_density(
        position.x, position.y, position.z, cloud::makeCloudParams(parameters));
}

float shadowTransmittance(const glm::vec3& receiver,
    const atmosphere::AtmosphereParameters& parameters, int steps, float extinction) {
    const glm::vec3 sun = atmosphere::sunDirection(parameters);
    if (!parameters.enabled || !parameters.cloudsEnabled || sun.y <= 0.02f
        || receiver.y >= parameters.cloudBaseHeight || steps <= 0) return 1.0f;
    const float start = (parameters.cloudBaseHeight - receiver.y) / sun.y;
    const float length = (parameters.cloudTopHeight - parameters.cloudBaseHeight) / sun.y;
    const float step = length / static_cast<float>(steps);
    double opticalDepth = 0.0;
    for (int index = 0; index < steps; ++index) {
        opticalDepth += densityAt(receiver + sun * (start + (index + 0.5f) * step), parameters) * step;
    }
    return transmission(static_cast<float>(opticalDepth * std::max(extinction, 0.0f)), parameters.cloudOfflineNoise);
}

float multiScatterPhase(float cosViewSun, int octaves, float attenuation, float eccentricity) {
    return myrenderer_cloud_multi_phase(cosViewSun, octaves, attenuation, eccentricity);
}

float powderFactor(float density, float distance) {
    return myrenderer_cloud_powder(density, distance);
}

SlabSpan slabSpan(
    float originY,
    float directionY,
    float baseHeight,
    float topHeight
) {
    SlabSpan span;
    const float base = std::min(baseHeight, topHeight);
    const float top = std::max(baseHeight, topHeight);
    // A ray parallel to the slab either misses it or is entirely inside it. Treating it as a miss is
    // the conservative choice: it cannot produce an infinite span.
    if (std::abs(directionY) < 1.0e-6f) return span;
    const float toBase = (base - originY) / directionY;
    const float toTop = (top - originY) / directionY;
    const float near = std::min(toBase, toTop);
    const float far = std::max(toBase, toTop);
    // Clamp the entry to the camera when the camera is inside the layer.
    const float start = std::max(near, 0.0f);
    const float length = far - start;
    if (length <= 0.0f) return span;
    span.valid = true;
    span.start = start;
    span.length = length;
    return span;
}

MarchResult march(
    const glm::vec3& origin,
    const glm::vec3& direction,
    const atmosphere::AtmosphereParameters& parameters,
    const MarchSettings& settings
) {
    MarchResult result;
    if (!parameters.cloudsEnabled || !parameters.enabled) return result;

    const glm::vec3 sun = settings.sunDirection;
    const SlabSpan span = clippedViewSpan(origin.y, direction.y, parameters);
    if (!span.valid) return result;

    const int steps = std::max(settings.primarySteps, 1);
    const float stepLength = span.length / static_cast<float>(steps);
    const float cosViewSun = glm::dot(direction, sun);
    const float extinction = std::max(settings.extinction, 0.0f);
    // Single scattering carries the sharp forward lobe; the octaves carry the diffuse fill that
    // single scattering cannot produce, because it counts only light arriving directly from the sun.
    const float singlePhase = myrenderer_cloud_phase(cosViewSun);
    const float multiPhase = myrenderer_cloud_multi_phase(
        cosViewSun, settings.multiScatterOctaves, settings.multiScatterAttenuation,
        settings.multiScatterEccentricity);
    const float multiWeight = std::clamp(settings.multiScatterAttenuation, 0.0f, 0.95f);

    // Front to back: each sample is composited behind everything already accumulated, which is what
    // makes the layer attenuate the background rather than replace it.
    glm::vec3 accumulated(0.0f);
    float transmittance = 1.0f;
    int unusedSamples = 0;
    for (int step = 0; step < steps; ++step) {
        // Centred sampling, plus the caller's sub-step offset. The offset is in world units so it
        // does not scale with the step count: a march that jitters proportionally would integrate a
        // slightly different function at every budget, and "converged" would have no meaning.
        const float distance = span.start
            + (static_cast<float>(step) + 0.5f) * stepLength + settings.jitter;
        if (distance < 0.0f || distance > span.start + span.length) continue;
        const glm::vec3 position = origin + direction * distance;
        const float density = densityAt(position, parameters);
        if (density <= 0.0f) continue;
        const float sampleOpticalDepth = density * stepLength;
        // The light march is only worth paying for where there is something to attenuate light.
        const float sunDepth = sunOpticalDepth(position, sun, parameters, settings, unusedSamples)
            * extinction;
        const float sunTransmittance = transmission(sunDepth, parameters.cloudOfflineNoise);
        // The fill gets an already-flattened transmittance: light that has bounced inside the cloud
        // arrives from every direction, so it is not extinguished by the same direct path depth. This
        // is the whole reason a multiply-scattered term is needed -- with the raw transmittance the
        // interior stays black however many octaves are added.
        const float fillTransmittance = transmission(sunDepth * multiWeight, parameters.cloudOfflineNoise);
        const float powder = settings.powder
            ? myrenderer_cloud_powder(density, stepLength) : 1.0f;
        const glm::vec3 scatter = settings.ambientRadiance * myrenderer_cloud_ambient_weight(
            position.y, parameters.cloudBaseHeight, parameters.cloudTopHeight, parameters.cloudHeightLighting)
            + settings.sunRadiance * ((1.0f - multiWeight) * singlePhase * sunTransmittance
                + multiWeight * multiPhase * fillTransmittance);
        const float stepTransmittance = transmission(sampleOpticalDepth * extinction, parameters.cloudOfflineNoise);
        // Energy-conserving in-scattering for the interval: the fraction of light that is both
        // scattered and not re-absorbed across this step.
        const float scattered = transmittance * (1.0f - stepTransmittance);
        accumulated += scatter * (scattered * powder);
        transmittance *= stepTransmittance;
        // No early exit on a low transmittance. The brief lists `T < 0.01` as a shader optimisation
        // and it is one, but here it would make the integral depend on *when* the threshold was
        // crossed rather than only on the step count, and this marcher is the golden value the GPU
        // march is compared against. A reference has to converge; the shader may cut corners.
    }
    const float horizon = horizonBlend(direction.y, parameters.cloudHorizonFadeDegrees);
    result.radiance = accumulated * horizon;
    result.transmittance = 1.0f - (1.0f - transmittance) * horizon;
    return result;
}

int densitySampleCount(
    const glm::vec3& origin,
    const glm::vec3& direction,
    const atmosphere::AtmosphereParameters& parameters,
    const MarchSettings& settings
) {
    if (!parameters.cloudsEnabled || !parameters.enabled) return 0;
    const SlabSpan span = clippedViewSpan(origin.y, direction.y, parameters);
    if (!span.valid) return 0;
    const int steps = std::max(settings.primarySteps, 1);
    const float stepLength = span.length / static_cast<float>(steps);
    int count = steps;
    for (int step = 0; step < steps; ++step) {
        const float distance = span.start
            + (static_cast<float>(step) + 0.5f) * stepLength + settings.jitter;
        if (distance < 0.0f || distance > span.start + span.length) continue;
        const glm::vec3 position = origin + direction * distance;
        if (densityAt(position, parameters) <= 0.0f) continue;
        count += std::max(settings.lightSteps, 1);
    }
    return count;
}

} // namespace cloud
