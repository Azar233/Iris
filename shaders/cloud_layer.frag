#version 330 core

// GPU ray march through the cloud layer (P1-A slice 6, step C2).
//
// The density field comes from `src/optics/CloudField.h`, the same file the CPU reference
// raymarcher and the analytic sky include. `cloud-field-parity` measures that the two compilers
// produce the same numbers from it (the integer hash is bit-identical; the rest agree to float
// rounding), which is why this shader and `cloud::march` can be compared directly rather than
// through a tolerance chosen by guesswork.
//
// Three attachments:
//   outCloud       = (scattered radiance.rgb, transmittance)
//   outCloudDebug  = (density samples taken, steps marched, slab span, phase)
//   outVolumeDepth = (first nonzero density distance, slab entry distance), zero on a miss
//
// The debug attachment exists so the cost model is measurable on the GPU the same way
// `cloud::densitySampleCount` makes it measurable on the CPU: a budget that cannot be counted is a
// budget that cannot be compared.
//
// Single-scattering transport plus C3's bounded phase-octave fill, shared with the CPU reference.

// The shared body marks its functions `MYRENDERER_CLOUD_INLINE`; the C++ adapter defines that as
// `inline`, and GLSL rejects the keyword, so here it is empty.
#define MYRENDERER_CLOUD_INLINE

#include "cloud_noise_sample.glsl"
#include "src/optics/CloudField.h"

in vec2 vUv;

uniform sampler2D uDepth;
uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;
uniform vec3 uCameraForward;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec2 uHalfExtent;      // tan(fov/2) * aspect, tan(fov/2)

uniform vec3 uAmbientRadiance;
uniform vec3 uSunRadiance;
uniform vec3 uSunDirection;
uniform float uExtinction;

uniform float uBaseHeight;
uniform float uTopHeight;
uniform float uFeatureScale;
uniform int uNoisePeriod;
uniform bool uOfflineNoise;
uniform vec2 uWind;
uniform float uCoverage;
uniform float uDensityScale;
// The weather map's controls (C5). They travel as loose uniforms and are assembled into the shared
// struct once in `main`, because the field's signature has to be one struct in both languages rather
// than a parameter list the shader and `CloudReference.cpp` each transcribe.
uniform float uWeatherScale;
uniform float uCoverageVariation;
uniform float uCloudType;
uniform float uTypeVariation;
uniform float uHeightVariation;
uniform float uDetailStrength;
uniform float uDetailEdge;
uniform float uMaximumViewDistance;
uniform float uHorizonFadeDegrees;
uniform int uPrimarySteps;
uniform int uLightSteps;
uniform float uJitter;         // world units, matching cloud::MarchSettings::jitter
uniform bool uSpatialJitter;
uniform int uJitterFrame;
uniform int uMultiScatterOctaves;
uniform float uMultiScatterAttenuation;
uniform float uMultiScatterEccentricity;
uniform bool uPowder;

layout(location = 0) out vec4 outCloud;
layout(location = 1) out vec4 outCloudDebug;
layout(location = 2) out vec2 outVolumeDepth;

// The thickness the slab is clipped against, in the same form as `cloud::slabSpan`. A ray parallel
// to the slab is a miss rather than an infinite span.
bool cloudSlabSpan(float originY, float directionY, out float start, out float length) {
    start = 0.0;
    length = 0.0;
    if (abs(directionY) < 1.0e-6) return false;
    float toBase = (uBaseHeight - originY) / directionY;
    float toTop = (uTopHeight - originY) / directionY;
    float near = min(toBase, toTop);
    float far = max(toBase, toTop);
    start = max(near, 0.0);
    length = far - start;
    return length > 0.0;
}

// Optical depth from `position` towards the sun, marched to the top of the slab. Stops at the slab
// exit rather than at a fixed distance, so a sample near the top does not pay for empty air above it.
float cloudSunDepth(vec3 position, MyRendererCloudParams layer, inout float sampleCount) {
    float start = 0.0;
    float length = 0.0;
    if (!cloudSlabSpan(position.y, uSunDirection.y, start, length)) return 0.0;
    int steps = max(uLightSteps, 1);
    float stepLength = length / float(steps);
    float depth = 0.0;
    for (int step = 0; step < steps; ++step) {
        float distance = start + (float(step) + 0.5) * stepLength;
        vec3 sample = position + uSunDirection * distance;
        depth += myrenderer_cloud_density(sample.x, sample.y, sample.z, layer) * stepLength;
        sampleCount += 1.0;
    }
    return depth;
}

void main() {
    vec3 direction = normalize(
        uCameraForward + uCameraRight * (vUv.x * 2.0 - 1.0) * uHalfExtent.x
            + uCameraUp * (vUv.y * 2.0 - 1.0) * uHalfExtent.y);

    MyRendererCloudParams layer;
    layer.baseHeight = uBaseHeight;
    layer.topHeight = uTopHeight;
    layer.featureScale = uFeatureScale;
    layer.noisePeriod = uNoisePeriod;
    layer.offlineNoise = uOfflineNoise;
    layer.windX = uWind.x;
    layer.windZ = uWind.y;
    layer.coverage = uCoverage;
    layer.densityScale = uDensityScale;
    layer.weatherScale = uWeatherScale;
    layer.coverageVariation = uCoverageVariation;
    layer.cloudType = uCloudType;
    layer.typeVariation = uTypeVariation;
    layer.heightVariation = uHeightVariation;
    layer.detailStrength = uDetailStrength;
    layer.detailEdge = uDetailEdge;

    // The background behind the cloud is the environment the sky was built into, so a cloud seen
    // against the sky is the same cloud the sky's own radiance passes through.
    vec3 accumulated = vec3(0.0);
    float transmittance = 1.0;
    float sampleCount = 0.0;
    // Single scattering carries the sharp forward lobe, the octaves the diffuse fill that single
    // scattering cannot produce. `multiWeight` is the fraction of the sun's energy handed to the fill.
    float singlePhase = myrenderer_cloud_phase(dot(direction, uSunDirection));
    float multiPhase = myrenderer_cloud_multi_phase(
        dot(direction, uSunDirection), uMultiScatterOctaves, uMultiScatterAttenuation,
        uMultiScatterEccentricity);
    float multiWeight = clamp(uMultiScatterAttenuation, 0.0, 0.95);
    float start = 0.0;
    float span = 0.0;
    float firstDepth = 0.0;

    if (cloudSlabSpan(uCameraPosition.y, direction.y, start, span)
        && start < uMaximumViewDistance) {
        span = min(span, uMaximumViewDistance - start);
        int steps = max(uPrimarySteps, 1);
        float stepLength = span / float(steps);
        // Integer hash has no short screen-space repeat. Fixing it to pixel coordinates makes
        // captures reproducible without temporal history, while avoiding aligned density slices.
        float pixelOffset = uSpatialJitter
            ? (myrenderer_cloud_hash_unit(int(gl_FragCoord.x) + uJitterFrame * 131,
                int(gl_FragCoord.y) + uJitterFrame * 977) - 0.5)
                * stepLength : 0.0;
        for (int step = 0; step < steps; ++step) {
            // Centred sampling, plus the caller's sub-step jitter in world units. The offset is
            // deliberately not a fraction of a step: scaling it with the budget would make each
            // budget integrate a slightly different function, and the CPU reference changed to world
            // units for exactly that reason.
            float distance = start + (float(step) + 0.5) * stepLength + uJitter + pixelOffset;
            if (distance < start || distance > start + span) continue;
            vec3 position = uCameraPosition + direction * distance;
            float density = myrenderer_cloud_density(
                position.x, position.y, position.z, layer);
            sampleCount += 1.0;
            if (density <= 0.0) continue;
            if (firstDepth == 0.0) firstDepth = distance;
            float sampleOpticalDepth = density * stepLength;
            float sunDepth = cloudSunDepth(position, layer, sampleCount) * uExtinction;
            float sunTransmittance = cloudTransmission(sunDepth, uOfflineNoise);
            // The fill gets an already-flattened transmittance: light that has bounced inside the
            // cloud arrives from every direction, so it is not extinguished by the same direct path
            // depth. Without this the interior stays black however many octaves are added.
            float fillTransmittance = cloudTransmission(sunDepth * multiWeight, uOfflineNoise);
            float powder = uPowder ? myrenderer_cloud_powder(density, stepLength) : 1.0;
            vec3 scatter = uAmbientRadiance
                + uSunRadiance * ((1.0 - multiWeight) * singlePhase * sunTransmittance
                    + multiWeight * multiPhase * fillTransmittance);
            float stepTransmittance = cloudTransmission(sampleOpticalDepth * uExtinction, uOfflineNoise);
            accumulated += scatter * (transmittance * (1.0 - stepTransmittance) * powder);
            transmittance *= stepTransmittance;
            // No early exit. The brief lists `T < 0.01` as a shader optimisation and it is one, but
            // the CPU reference deliberately does not take it because it would make the integral
            // depend on *when* the threshold was crossed rather than only on the step count. Two
            // sides of a comparison have to integrate the same function.
        }
    }

    float horizon = 1.0;
    if (uHorizonFadeDegrees > 0.0) {
        float edge = sin(radians(clamp(uHorizonFadeDegrees, 0.0, 30.0)));
        horizon = smoothstep(0.0, max(edge, 1.0e-6), max(direction.y, 0.0));
    }
    accumulated *= horizon;
    transmittance = 1.0 - (1.0 - transmittance) * horizon;

    outCloud = vec4(accumulated, transmittance);
    outCloudDebug = vec4(sampleCount, float(uPrimarySteps), span, singlePhase);
    outVolumeDepth = horizon > 0.0 && firstDepth > 0.0 ? vec2(firstDepth, start) : vec2(0.0);
}
