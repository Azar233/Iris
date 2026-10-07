// CPU reference raymarcher contracts for the cloud layer (P1-A slice 6, step C2).
//
// These tests exist because the volumetric step is where cloud rendering stops being a formula and
// becomes an integral, and an integral has properties that can be asserted without looking at an
// image: it must conserve energy, it must respond monotonically to density and to the sun, it must
// be deterministic, and it must cost what the cost model says it costs. The reference is also the
// golden value the GPU march is compared against, so its own behaviour has to be pinned first.
#include "optics/CloudReference.h"
#include "optics/CloudFieldCpp.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string describe(const glm::vec3& value) {
    return "(" + std::to_string(value.x) + ", " + std::to_string(value.y) + ", "
        + std::to_string(value.z) + ")";
}

bool allFiniteNonNegative(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
        && value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f;
}

atmosphere::AtmosphereParameters cloudParameters() {
    atmosphere::AtmosphereParameters parameters;
    parameters.enabled = true;
    parameters.sunElevationDegrees = 45.0f;
    parameters.sunAzimuthDegrees = 136.0f;
    parameters.skyIntensity = 3.0f;
    parameters.sunIntensity = 3.0f;
    parameters.groundAlbedo = 0.25f;
    parameters.cloudsEnabled = true;
    return parameters;
}

cloud::MarchSettings marchSettings(const atmosphere::AtmosphereParameters& parameters) {
    cloud::MarchSettings settings;
    settings.primarySteps = 32;
    settings.lightSteps = 6;
    settings.extinction = 0.0025f;
    settings.jitter = 0.0f;
    // The same sky the analytic layer is lit by, from the same model, so the two are comparable.
    settings.ambientRadiance = atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), parameters)
        * 0.85f;
    settings.sunDirection = atmosphere::sunDirection(parameters);
    settings.sunRadiance = atmosphere::skyLightColor(parameters)
        * std::max(parameters.sunIntensity, 0.0f);
    return settings;
}

// Sweeps the directions a ground-level camera actually looks at.
std::vector<glm::vec3> viewDirections() {
    std::vector<glm::vec3> directions;
    for (float elevation : {2.0f, 8.0f, 20.0f, 40.0f, 70.0f}) {
        for (int azimuth = 0; azimuth < 8; ++azimuth) {
            const float e = glm::radians(elevation);
            const float a = glm::radians(static_cast<float>(azimuth) * 45.0f);
            directions.push_back(glm::vec3(
                std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)));
        }
    }
    return directions;
}

} // namespace

int main() {
    try {
        const atmosphere::AtmosphereParameters parameters = cloudParameters();
        const cloud::MarchSettings settings = marchSettings(parameters);
        constexpr float eyeHeight = 1.1f;

        // 1. A disabled layer is not merely faint, it is absent: full transmittance and no radiance
        //    at every direction, which is what keeps every scene written before this slice identical.
        {
            atmosphere::AtmosphereParameters off = parameters;
            off.cloudsEnabled = false;
            for (const glm::vec3& direction : viewDirections()) {
                const cloud::MarchResult result = cloud::march(
                    glm::vec3(0.0f, eyeHeight, 0.0f), direction, off, settings);
                require(result.radiance == glm::vec3(0.0f),
                    "a disabled layer must scatter nothing");
                require(result.transmittance == 1.0f,
                    "a disabled layer must not attenuate the background");
                require(cloud::densitySampleCount(
                            glm::vec3(0.0f, eyeHeight, 0.0f), direction, off, settings) == 0,
                    "a disabled layer must not be sampled");
            }
        }

        // 2. The slab span is the closed form the march is built on, and it must agree with the ray
        //    geometry at both limits: a vertical ray from the ground crosses exactly the slab, and a
        //    ray that misses the slab is reported as a miss rather than as an infinite span.
        {
            const float base = parameters.cloudBaseHeight;
            const float top = parameters.cloudTopHeight;
            const cloud::SlabSpan vertical = cloud::slabSpan(eyeHeight, 1.0f, base, top);
            require(vertical.valid, "a vertical ray must cross the slab");
            // The span is measured along the ray, so the boundary heights are recovered by walking
            // the distance from the camera rather than by comparing distances to heights.
            const float entryHeight = eyeHeight + vertical.start;
            const float exitHeight = eyeHeight + vertical.start + vertical.length;
            require(std::abs(entryHeight - base) < 1.0e-2f,
                "a vertical ray from the ground must enter at the slab base");
            require(std::abs(exitHeight - top) < 1.0e-2f,
                "a vertical ray from the ground must exit at the slab top");
            require(std::abs(vertical.length - (top - base)) < 1.0e-2f,
                "a vertical ray must cross exactly the slab's thickness");
            const cloud::SlabSpan grazing = cloud::slabSpan(eyeHeight, 0.0f, base, top);
            require(!grazing.valid, "a ray parallel to the slab must be a miss");
            const cloud::SlabSpan inside = cloud::slabSpan(
                0.5f * (base + top), 1.0f, base, top);
            require(inside.valid && inside.start == 0.0f,
                "a camera inside the layer must start marching immediately");
            const cloud::SlabSpan inverted = cloud::slabSpan(eyeHeight, 1.0f, top, base);
            require(inverted.valid,
                "an inverted slab must be normalised rather than reported as a miss");
        }

        // Cellular noise must be continuous across cell boundaries in all three axes. Missing
        // neighbours used to print the lattice into the image, while the old 2D field repeated the
        // same cross-section through the entire slab.
        {
            int varyingWithHeight = 0;
            for (int probe = 0; probe < 64; ++probe) {
                const float x = -2.0f + static_cast<float>(probe) * 0.125f;
                const float y = 0.13f + static_cast<float>(probe % 13) * 0.23f;
                const float z = 0.27f + static_cast<float>(probe % 11) * 0.19f;
                constexpr float epsilon = 1.0e-4f;
                for (int axis = 0; axis < 3; ++axis) {
                    const float boundary = static_cast<float>(probe % 4 - 2);
                    const auto valueAt = [&](float offset) {
                        return myrenderer_cloud_worley3(
                            axis == 0 ? boundary + offset : x,
                            axis == 1 ? boundary + offset : y,
                            axis == 2 ? boundary + offset : z, 4, 101);
                    };
                    require(std::abs(valueAt(-epsilon) - valueAt(epsilon)) < 0.001f,
                        "3D Worley must be continuous across every lattice axis");
                }
                const float value = myrenderer_cloud_worley3(x, y, z, 4, 101);
                require(std::abs(value - myrenderer_cloud_worley3(x + 4.0f, y, z, 4, 101))
                        < 2.0e-6f,
                    "the 3D shape must preserve the configured horizontal period");
                if (std::abs(value - myrenderer_cloud_worley3(x, y, z + 0.5f, 4, 101))
                        > 0.01f) ++varyingWithHeight;
            }
            require(varyingWithHeight > 32,
                "the cloud volume must have independent structure through height");
        }

        // The persisted noise period must reach the density field. Before this contract existed,
        // the field ignored `cloudNoisePeriod` and repeated after every single feature scale, which
        // projected into conspicuous radial stripes near the horizon.
        {
            atmosphere::AtmosphereParameters periodic = parameters;
            periodic.cloudNoisePeriod = 4.0f;
            periodic.cloudCoverageVariation = 0.0f;
            periodic.cloudTypeVariation = 0.0f;
            periodic.cloudHeightVariation = 0.0f;
            const float fullPeriod = periodic.cloudFeatureScale * periodic.cloudNoisePeriod;
            int differentInsidePeriod = 0;
            for (int index = 0; index < 64; ++index) {
                const glm::vec3 point(
                    173.0f + static_cast<float>(index) * 311.0f,
                    0.5f * (periodic.cloudBaseHeight + periodic.cloudTopHeight),
                    -419.0f + static_cast<float>(index) * 197.0f);
                const float density = cloud::densityAt(point, periodic);
                require(std::abs(cloud::densityAt(
                            point + glm::vec3(fullPeriod, 0.0f, 0.0f), periodic) - density)
                            < 2.0e-5f,
                    "one configured noise period along X must reproduce the density");
                require(std::abs(cloud::densityAt(
                            point + glm::vec3(0.0f, 0.0f, fullPeriod), periodic) - density)
                            < 2.0e-5f,
                    "one configured noise period along Z must reproduce the density");
                if (cloud::densityAt(
                        point + glm::vec3(periodic.cloudFeatureScale, 0.0f, 0.0f), periodic)
                    != density) {
                    ++differentInsidePeriod;
                }
            }
            require(differentInsidePeriod > 8,
                "the density must not repeat after one feature scale when the period is four");
        }

        // 3. Every direction produces a physical result: finite, non-negative radiance and a
        //    transmittance that is a fraction.
        for (const glm::vec3& direction : viewDirections()) {
            const cloud::MarchResult result = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, settings);
            require(allFiniteNonNegative(result.radiance),
                ("the march must produce finite radiance " + describe(result.radiance)).c_str());
            require(result.transmittance >= 0.0f && result.transmittance <= 1.0f,
                "the march must produce a transmittance in 0..1");
        }

        // 4. Density is the one input that must move opacity monotonically: more cloud means less
        //    background reaches the camera, at every direction.
        {
            atmosphere::AtmosphereParameters thicker = parameters;
            thicker.cloudDensity = 3.0f;
            for (const glm::vec3& direction : viewDirections()) {
                const cloud::MarchResult thin = cloud::march(
                    glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, settings);
                const cloud::MarchResult dense = cloud::march(
                    glm::vec3(0.0f, eyeHeight, 0.0f), direction, thicker, settings);
                require(dense.transmittance <= thin.transmittance + 1.0e-6f,
                    "a denser layer must not transmit more background");
            }
        }

        // 5. Coverage is a fraction of the sky, so lowering it can only add background back. C1
        //    shipped the opposite direction and C5 flipped it to match the weather map's red channel.
        {
            atmosphere::AtmosphereParameters clear = parameters;
            clear.cloudCoverage = 0.05f;
            for (const glm::vec3& direction : viewDirections()) {
                const cloud::MarchResult covered = cloud::march(
                    glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, settings);
                const cloud::MarchResult open = cloud::march(
                    glm::vec3(0.0f, eyeHeight, 0.0f), direction, clear, settings);
                require(open.transmittance >= covered.transmittance - 1.0e-6f,
                    "lowering the coverage fraction must not occlude more sky");
            }
        }

        // 6. The phase function has to be visible in the result: looking towards the sun through the
        //    layer must scatter more than looking away from it, at the same elevation. The probe
        //    pins the layer to full, uniform coverage so both directions are genuinely inside cloud
        //    -- without that the comparison measures which ray happened to find a gap, not the phase
        //    function, and a weather map with gaps in it makes that failure intermittent.
        {
            atmosphere::AtmosphereParameters lit = parameters;
            lit.cloudCoverage = 1.0f;
            lit.cloudCoverageVariation = 0.0f;
            lit.cloudTypeVariation = 0.0f;
            lit.cloudHeightVariation = 0.0f;
            const glm::vec3 sun = settings.sunDirection;
            const float sunAzimuth = std::atan2(sun.x, sun.z);
            double forward = 0.0;
            double backward = 0.0;
            int covered = 0;
            const auto luminance = [](const glm::vec3& value) {
                return 0.2126 * value.r + 0.7152 * value.g + 0.0722 * value.b;
            };
            for (float elevationDegrees : {20.0f, 30.0f, 45.0f, 60.0f}) {
                const float elevation = glm::radians(elevationDegrees);
                const auto towardSun = glm::vec3(
                    std::cos(elevation) * std::sin(sunAzimuth), std::sin(elevation),
                    std::cos(elevation) * std::cos(sunAzimuth));
                const auto awayFromSun = glm::vec3(
                    std::cos(elevation) * std::sin(sunAzimuth + 3.14159265358979323846f),
                    std::sin(elevation),
                    std::cos(elevation) * std::cos(sunAzimuth + 3.14159265358979323846f));
                const cloud::MarchResult forwardResult = cloud::march(
                    glm::vec3(0.0f, eyeHeight, 0.0f), towardSun, lit, settings);
                const cloud::MarchResult backwardResult = cloud::march(
                    glm::vec3(0.0f, eyeHeight, 0.0f), awayFromSun, lit, settings);
                require(forwardResult.transmittance < 0.99f
                            && backwardResult.transmittance < 0.99f,
                    "the phase probe needs both rays to cross cloud, or it measures coverage");
                forward += luminance(forwardResult.radiance);
                backward += luminance(backwardResult.radiance);
                ++covered;
            }
            require(covered == 4, "the phase probe must run at every elevation");
            std::cout << "  phase probe: forward " << forward << ", backward " << backward << '\n';
            require(forward > backward,
                "the layer must scatter more towards the sun than away from it");
        }

        // 7. Wind is a translation of the field, so moving the camera by the same vector must give
        //    the same density at corresponding points: the layer is a field in world space, not a
        //    screen effect. Asserted on the density rather than on a march result, because that is
        //    where the property actually lives.
        {
            const float wind = 500.0f;
            atmosphere::AtmosphereParameters shifted = parameters;
            shifted.cloudWindOffsetX = wind;
            const float sampleHeight = parameters.cloudBaseHeight + 100.0f;
            for (float offset : {0.0f, 137.5f, 900.0f, 12345.0f}) {
                const float baseDensity = cloud::densityAt(
                    glm::vec3(offset, sampleHeight, -offset), parameters);
                const float advectedDensity = cloud::densityAt(
                    glm::vec3(offset + wind, sampleHeight, -offset), shifted);
                require(std::abs(baseDensity - advectedDensity) < 1.0e-4f,
                    ("advecting the layer must be equivalent to moving the sample [base="
                        + std::to_string(baseDensity) + " advected="
                        + std::to_string(advectedDensity) + " at "
                        + std::to_string(offset) + "]").c_str());
            }
        }

        // 8. Determinism: the same arguments must give bit-for-bit the same result, because a
        //    baseline is only comparable if the reference is reproducible.
        {
            const glm::vec3 direction = glm::normalize(glm::vec3(-0.4f, 0.6f, 0.7f));
            const cloud::MarchResult first = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, settings);
            const cloud::MarchResult second = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, settings);
            require(first.radiance == second.radiance
                    && first.transmittance == second.transmittance,
                "the reference march must be deterministic");
        }

        // 9. The cost model has to be the real one: a ray that never enters cloud pays only for the
        //    primary march, and a ray that does pays for the light march of every covered sample.
        {
            const glm::vec3 covered = glm::normalize(glm::vec3(0.0f, 0.4f, 1.0f));
            const int count = cloud::densitySampleCount(
                glm::vec3(0.0f, eyeHeight, 0.0f), covered, parameters, settings);
            require(count >= settings.primarySteps,
                "the primary march must be paid for in full");
            require(count <= settings.primarySteps * (settings.lightSteps + 1),
                "the light march must not exceed one per primary sample");
            const glm::vec3 downward(0.0f, -1.0f, 0.0f);
            require(cloud::densitySampleCount(
                        glm::vec3(0.0f, eyeHeight, 0.0f), downward, parameters, settings) == 0,
                "a ray that misses the slab must cost nothing");
        }

        // 10. The step budget must not be cosmetic: it has to change the integral, or the march is
        //     not resolving the field it claims to resolve. Note what is *not* asserted: that the
        //     march converges as steps grow. The density field has a kink wherever the shape crosses
        //     the coverage threshold, so a fixed step count is the contract -- which is exactly what
        //     `determinism` in C7 will pin, and what the GPU comparison is measured at. Demanding
        //     convergence here would be demanding a property the model does not have.
        {
            cloud::MarchSettings coarse = settings;
            coarse.primarySteps = 16;
            cloud::MarchSettings fine = settings;
            fine.primarySteps = 128;
            const glm::vec3 direction = glm::normalize(glm::vec3(0.2f, 0.45f, 0.9f));
            const cloud::MarchResult coarseResult = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, coarse);
            const cloud::MarchResult fineResult = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, fine);
            require(std::abs(coarseResult.transmittance - fineResult.transmittance) > 1.0e-4f,
                "the step count must actually change the integral");
            // And the budget is reported, not implied: the caller can always say what it cost.
            require(cloud::densitySampleCount(
                        glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, fine)
                    >= cloud::densitySampleCount(
                        glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, coarse),
                "a larger budget must report at least as many samples");
        }

        // 11. Jitter has to move the first sample, which is what it is for: a different jitter is a
        //     different sample placement, and the deterministic mode will rely on being able to fix
        //     it to a reproducible sequence.
        //
        //     The direction is *found* rather than assumed. An earlier revision hard-coded one that
        //     happened to miss the slab at one cloud height and hit it at another, so it compared two
        //     transmittances of 1 and reported that the jitter did nothing. A test whose setup is not
        //     checked is a test that reports on its own setup.
        {
            cloud::MarchSettings offset = settings;
            // Scaled to the extinction regime rather than fixed: at the calibrated extinction a
            // step's optical depth is small, so a small offset moves the result by less than the
            // assertion's own tolerance.
            offset.jitter = 2.0f;
            glm::vec3 direction(0.0f, 1.0f, 0.0f);
            bool foundCloud = false;
            for (int step = 0; step < 72 && !foundCloud; ++step) {
                const float azimuth = 2.0f * 3.14159265358979323846f
                    * static_cast<float>(step) / 72.0f;
                const float elevation = glm::radians(35.0f);
                const glm::vec3 candidate(
                    std::cos(elevation) * std::sin(azimuth),
                    std::sin(elevation),
                    std::cos(elevation) * std::cos(azimuth));
                const cloud::MarchResult probe = cloud::march(
                    glm::vec3(0.0f, eyeHeight, 0.0f), candidate, parameters, settings);
                if (probe.transmittance < 0.9f) {
                    direction = candidate;
                    foundCloud = true;
                }
            }
            require(foundCloud, "the probe frame must contain cloud to test the jitter against");
            const cloud::MarchResult centred = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, settings);
            const cloud::MarchResult jittered = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, offset);
            require(std::abs(centred.transmittance - jittered.transmittance) > 1.0e-5f,
                "the jitter must move the sampling");
            require(jittered.transmittance >= 0.0f && jittered.transmittance <= 1.0f,
                "a jittered march must stay physical");
        }

        // 12. Hostile inputs must stay finite: a camera inside the layer, above it, inside the
        //     ground, a collapsed and an inverted slab, and a sun below the horizon.
        {
            for (float height : {-1.0e4f, 0.0f, 4000.0f, 8000.0f, 1.0e6f}) {
                for (const glm::vec3& direction : viewDirections()) {
                    const cloud::MarchResult result = cloud::march(
                        glm::vec3(0.0f, height, 0.0f), direction, parameters, settings);
                    require(allFiniteNonNegative(result.radiance),
                        "every camera height must produce finite radiance");
                    require(result.transmittance >= 0.0f && result.transmittance <= 1.0f,
                        "every camera height must produce a fraction");
                }
            }
            atmosphere::AtmosphereParameters flat = parameters;
            flat.cloudTopHeight = flat.cloudBaseHeight;
            atmosphere::AtmosphereParameters inverted = parameters;
            inverted.cloudBaseHeight = inverted.cloudTopHeight + 500.0f;
            atmosphere::AtmosphereParameters hostile = parameters;
            hostile.cloudDensity = -4.0f;
            hostile.cloudCoverage = -2.0f;
            hostile.cloudFeatureScale = -100.0f;
            for (const auto* variant : {&flat, &inverted, &hostile}) {
                for (const glm::vec3& direction : viewDirections()) {
                    const cloud::MarchResult result = cloud::march(
                        glm::vec3(0.0f, eyeHeight, 0.0f), direction, *variant, settings);
                    require(allFiniteNonNegative(result.radiance),
                        "a degenerate slab must not produce NaN or negative radiance");
                    require(result.transmittance >= 0.0f && result.transmittance <= 1.0f,
                        "a degenerate slab must not produce an invalid transmittance");
                }
            }
            atmosphere::AtmosphereParameters night = parameters;
            night.sunElevationDegrees = -20.0f;
            cloud::MarchSettings nightSettings = marchSettings(night);
            const cloud::MarchResult nightResult = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f),
                glm::normalize(glm::vec3(0.0f, 0.5f, 1.0f)), night, nightSettings);
            require(allFiniteNonNegative(nightResult.radiance),
                "a sun below the horizon must not produce NaN");
        }

        // 13. A very thin layer must converge on the analytic limit: as extinction goes to zero the
        //     layer becomes transparent and its radiance goes to zero, which is the one limit where
        //     the volumetric march and the analytic layer must agree exactly.
        {
            cloud::MarchSettings transparent = settings;
            transparent.extinction = 0.0f;
            const glm::vec3 direction = glm::normalize(glm::vec3(0.0f, 0.5f, 1.0f));
            const cloud::MarchResult clear = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, transparent);
            require(std::abs(clear.transmittance - 1.0f) < 1.0e-6f,
                "zero extinction must leave the background untouched");
            require(glm::length(clear.radiance) < 1.0e-6f,
                "zero extinction must scatter nothing");
        }

        // 14. The volumetric luminance contract, stated numerically for the same reason C1's is:
        //     "looks like cloud" is not testable and the measured ratios are. C1's coefficients were
        //     calibrated for a *single* density sample per direction; a march accumulates dozens of
        //     them, so the first frame with the march switched on blew the sky out to white. The
        //     bound below is the calibrated state, measured through `MyRendererCloudCalibration` and
        //     then against rendered pixels: a cloud must be within reach of the sky's own brightness
        //     so the layer reads as cloud rather than as a black stain or as a blown-out sheet.
        {
            atmosphere::AtmosphereParameters lit = parameters;
            lit.cloudVolumetricAmbientScale = 4.0f;
            lit.cloudVolumetricSunScale = 0.05f;
            cloud::MarchSettings litSettings = marchSettings(lit);
            litSettings.extinction = cloud::volumetricExtinction;
            // The lighting comes from `marchLighting`, which owns the volumetric scales. Using the
            // generic helper above instead would leave the analytic layer's single-sample ambient in
            // place and calibrate against a cloud four times too dark -- which is exactly what an
            // earlier revision of this contract did.
            const cloud::MarchLighting lighting = cloud::marchLighting(lit);
            litSettings.ambientRadiance = lighting.ambient;
            litSettings.sunRadiance = lighting.sun;
            const auto luminance = [](const glm::vec3& value) {
                return 0.2126 * value.r + 0.7152 * value.g + 0.0722 * value.b;
            };
            // Expressed relative to the *zenith* sky rather than to the sky behind the ray. A ratio
            // against the anti-solar sky looks enormous because that sky is dim, which says nothing
            // about whether the cloud is too bright; the zenith is a stable reference that catches
            // both failure modes, and the bounds below bracket the measured 5.8x.
            const float zenithLuminance = std::max(
                static_cast<float>(luminance(
                    atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), lit))), 1.0e-6f);
            const auto cloudLuminance = [&](float elevationDegrees) {
                const float elevation = glm::radians(elevationDegrees);
                double total = 0.0;
                int counted = 0;
                for (int step = 0; step < 36; ++step) {
                    const float azimuth = 2.0f * 3.14159265358979323846f
                        * static_cast<float>(step) / 36.0f;
                    const glm::vec3 direction(
                        std::cos(elevation) * std::sin(azimuth),
                        std::sin(elevation),
                        std::cos(elevation) * std::cos(azimuth));
                    const cloud::MarchResult marched = cloud::march(
                        glm::vec3(0.0f, eyeHeight, 0.0f), direction, lit, litSettings);
                    total += luminance(marched.radiance);
                    ++counted;
                }
                return counted > 0 ? total / counted : 0.0;
            };
            const double nearHorizon = cloudLuminance(5.0f) / zenithLuminance;
            const double highSky = cloudLuminance(45.0f) / zenithLuminance;
            std::cout << "  marched cloud / zenith sky: 5deg " << nearHorizon
                << ", 45deg " << highSky << '\n';
            require(nearHorizon > 0.5,
                "a distance-clipped cloud near the horizon must remain visibly lit");
            require(highSky < 20.0,
                "a marched cloud must not blow the sky out to white");
            // What is *not* asserted is brightness rising with elevation. The analytic layer did that
            // because it scaled opacity by the slab crossing; a march integrates the real path, and
            // the measured result is roughly flat at 4.1 and 5.0. Asserting the analytic behaviour
            // here would be asserting a property this model does not have.
            //
            // The mechanism worth pinning instead is extinction. It is the parameter that makes the
            // layer opaque, and if it stops working the calibration above turns into a brightness
            // setting for a translucent haze.
            const auto meanTransmittance = [&](float extinction) {
                cloud::MarchSettings probe = litSettings;
                probe.extinction = extinction;
                double total = 0.0;
                int counted = 0;
                const float elevation = glm::radians(30.0f);
                for (int step = 0; step < 36; ++step) {
                    const float azimuth = 2.0f * 3.14159265358979323846f
                        * static_cast<float>(step) / 36.0f;
                    const glm::vec3 direction(
                        std::cos(elevation) * std::sin(azimuth),
                        std::sin(elevation),
                        std::cos(elevation) * std::cos(azimuth));
                    total += cloud::march(
                        glm::vec3(0.0f, eyeHeight, 0.0f), direction, lit, probe).transmittance;
                    ++counted;
                }
                return counted > 0 ? total / counted : 1.0;
            };
            const double calibratedOpacity = meanTransmittance(cloud::volumetricExtinction);
            const double thinner = meanTransmittance(cloud::volumetricExtinction * 0.2f);
            require(thinner > calibratedOpacity + 0.05,
                "extinction must be what makes the layer opaque");
            require(calibratedOpacity < 0.8,
                "the calibrated extinction must leave the layer substantially opaque");
        }

        // 14b. A nearly horizontal ray used to traverse hundreds of kilometres of a flat periodic
        //     slab. Perspective projected those repeats into radial bands. The renderer now limits
        //     the primary view march to the useful reach of the configured cloud field, while the
        //     horizon fade makes the cutoff continuous. Pin both pieces here so the artifact cannot
        //     return through either the CPU reference or the GPU parity contract.
        {
            const float maximumDistance = cloud::maximumViewDistance(parameters);
            require(std::isfinite(maximumDistance) && maximumDistance >= 10000.0f
                    && maximumDistance <= 200000.0f,
                "the cloud view distance must stay finite and within its safety bounds");

            const float shallowElevation = glm::radians(1.0f);
            const glm::vec3 shallowDirection(
                0.0f, std::sin(shallowElevation), std::cos(shallowElevation));
            require(cloud::densitySampleCount(
                        glm::vec3(0.0f, eyeHeight, 0.0f), shallowDirection,
                        parameters, settings) == 0,
                "a shallow ray whose cloud entry lies beyond the useful field must not march");
            const cloud::MarchResult shallow = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), shallowDirection, parameters, settings);
            require(shallow.radiance == glm::vec3(0.0f) && shallow.transmittance == 1.0f,
                "a clipped shallow ray must leave the sky background untouched");

            atmosphere::AtmosphereParameters noFade = parameters;
            noFade.cloudHorizonFadeDegrees = 0.0f;
            atmosphere::AtmosphereParameters faded = parameters;
            faded.cloudHorizonFadeDegrees = 5.0f;
            const float fadeElevation = glm::radians(4.0f);
            bool fadeChangedCloud = false;
            for (int azimuthStep = 0; azimuthStep < 16; ++azimuthStep) {
                const float azimuth = glm::radians(22.5f * static_cast<float>(azimuthStep));
                const glm::vec3 direction(
                    std::cos(fadeElevation) * std::sin(azimuth),
                    std::sin(fadeElevation),
                    std::cos(fadeElevation) * std::cos(azimuth));
                const cloud::MarchResult raw = cloud::march(
                    glm::vec3(0.0f, eyeHeight, 0.0f), direction, noFade, settings);
                const cloud::MarchResult blended = cloud::march(
                    glm::vec3(0.0f, eyeHeight, 0.0f), direction, faded, settings);
                require(blended.transmittance + 1.0e-6f >= raw.transmittance,
                    "the horizon fade must not make a cloud more opaque");
                require(glm::length(blended.radiance) <= glm::length(raw.radiance) + 1.0e-6f,
                    "the horizon fade must not add cloud radiance");
                fadeChangedCloud = fadeChangedCloud
                    || blended.transmittance > raw.transmittance + 1.0e-5f;
            }
            require(fadeChangedCloud,
                "the horizon fade sweep must encounter and soften at least one cloud ray");
        }

        // 15. The multiple-scattering approximation (C3). Its contract is not "brighter" -- the
        //     octave weights are a geometric series summing to one, so the term redistributes energy
        //     rather than adding it, and brightness stays the calibrated scales' job. What has to hold
        //     is that it is a *phase function* (mean 1 over the sphere), that the fill grows with the
        //     attenuation weight, and that switching it off changes the integral -- a term that cannot
        //     move the result is a term that is not connected.
        {
            const int phaseSteps = 4000;
            const auto integratePhase = [&](int octaves, float attenuation, float eccentricity) {
                double total = 0.0;
                for (int index = 0; index < phaseSteps; ++index) {
                    const float cosine = -1.0f + 2.0f * (static_cast<float>(index) + 0.5f)
                        / static_cast<float>(phaseSteps);
                    total += cloud::multiScatterPhase(
                        cosine, octaves, attenuation, eccentricity);
                }
                return total * (2.0 / static_cast<double>(phaseSteps)) * 0.5;
            };
            std::cout << "  multi-scatter probes: p(1)="
                << cloud::multiScatterPhase(1.0f, 4, 0.6f, 0.6f)
                << " p(0)=" << cloud::multiScatterPhase(0.0f, 4, 0.6f, 0.6f)
                << " p(-1)=" << cloud::multiScatterPhase(-1.0f, 4, 0.6f, 0.6f)
                << " | single p(1)=" << atmosphere::cloudPhase(1.0f)
                << " p(0)=" << atmosphere::cloudPhase(0.0f) << '\n';
            std::cout << "  multi-scatter phase integral: "
                << integratePhase(4, 0.6f, 0.6f) << " (4 octaves), "
                << integratePhase(1, 0.6f, 0.6f) << " (1 octave)\n";
            require(std::abs(integratePhase(4, 0.6f, 0.6f) - 1.0) < 0.05,
                "the multiple-scattering phase function must integrate to one over the sphere");
            require(std::abs(integratePhase(1, 0.6f, 0.6f) - 1.0) < 0.05,
                "a single octave of it must integrate to one as well");
            // More octaves flatten the lobe: the forward peak has to come down, because the same
            // energy is being spread over more directions.
            const float oneOctavePeak = cloud::multiScatterPhase(1.0f, 1, 0.6f, 0.6f);
            const float fourOctavePeak = cloud::multiScatterPhase(1.0f, 4, 0.6f, 0.6f);
            require(fourOctavePeak < oneOctavePeak,
                "more octaves must spread the forward peak rather than sharpen it");
            // And the term has to be connected to the integral.
            const glm::vec3 direction = glm::normalize(glm::vec3(0.25f, 0.5f, 0.83f));
            cloud::MarchSettings withoutMulti = settings;
            withoutMulti.multiScatterAttenuation = 0.0f;
            const cloud::MarchResult single = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, withoutMulti);
            const cloud::MarchResult multi = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, settings);
            require(glm::length(multi.radiance - single.radiance) > 1.0e-4f,
                "the multiple-scattering term must change the integral");
            // Transmittance is unaffected: it is extinction, and the fill does not move light along
            // the view ray.
            require(std::abs(multi.transmittance - single.transmittance) < 1.0e-6f,
                "the multiple-scattering fill must not change transmittance");
            auto heightParameters = parameters;
            heightParameters.cloudHeightLighting = true;
            auto nightParameters = heightParameters;
            nightParameters.sunElevationDegrees = -12.0f;
            require(glm::length(cloud::marchLighting(nightParameters).sun) == 0.0f,
                "height lighting must not illuminate night clouds with a below-horizon sun");
            require(!atmosphere::parametersMatch(parameters, heightParameters),
                "height lighting changes must invalidate cloud history");
            auto ambientOnly = settings;
            ambientOnly.sunRadiance = glm::vec3(0.0f);
            const auto uniformAmbient = cloud::march(glm::vec3(0.0f, eyeHeight, 0.0f),
                direction, parameters, ambientOnly);
            const auto heightAmbient = cloud::march(glm::vec3(0.0f, eyeHeight, 0.0f),
                direction, heightParameters, ambientOnly);
            require(glm::length(heightAmbient.radiance) < glm::length(uniformAmbient.radiance)
                && heightAmbient.transmittance == uniformAmbient.transmittance,
                "height ambient must reduce buried radiance without changing extinction");
            // The powder term darkens thin edges, so switching it off must brighten the frame.
            cloud::MarchSettings noPowder = settings;
            noPowder.powder = false;
            const cloud::MarchResult powdered = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, settings);
            const cloud::MarchResult plain = cloud::march(
                glm::vec3(0.0f, eyeHeight, 0.0f), direction, parameters, noPowder);
            require(glm::length(plain.radiance) >= glm::length(powdered.radiance) - 1.0e-6f,
                "the powder term must not brighten a cloud");
        }

        // 16. The weather map and the base/detail split (C5), asserted on the density field because
        //     that is where the properties live.
        //
        //     The contract that matters is the one C2 failed: a detail octave summed into the field
        //     *before* the coverage threshold puts its own blobs wherever the sum sits near the
        //     threshold, so the layer's silhouette becomes a filigree. The fix is structural, so the
        //     test is too: the finest octaves may only ever take density away from a cloud the base
        //     shape already formed. "Detail cannot create cloud" is checkable exactly, and it is the
        //     property that no parameter combination in C2 had.
        {
            // The control: with the detail weight at zero the density *is* the thresholded base
            // field, so one call gives both halves of every comparison below.
            atmosphere::AtmosphereParameters baseOnly = parameters;
            baseOnly.cloudDetailStrength = 0.0f;
            atmosphere::AtmosphereParameters carved = parameters;
            carved.cloudDetailStrength = 1.0f;
            carved.cloudDetailEdge = 0.0f;
            atmosphere::AtmosphereParameters carvedToTheEdge = parameters;
            carvedToTheEdge.cloudDetailStrength = 1.0f;
            carvedToTheEdge.cloudDetailEdge = 1.0f;

            std::size_t solid = 0U;
            std::size_t emptied = 0U;
            for (int x = 0; x < 48; ++x) {
                for (int z = 0; z < 48; ++z) {
                    // Three heights well inside the slab. The profile is a bump, not a plateau, so
                    // probes near the base or the top would be measuring the envelope rather than
                    // the erosion; 0.33 / 0.5 / 0.67 of the way up all sit where the profile is
                    // close to one.
                    for (float y : {4800.0f, 5600.0f, 6400.0f}) {
                        const glm::vec3 position(
                            -12000.0f + 500.0f * static_cast<float>(x),
                            y,
                            -12000.0f + 500.0f * static_cast<float>(z));
                        const float base = cloud::densityAt(position, baseOnly);
                        if (base > 0.0f) ++solid;
                        for (const auto* variant : {&carved, &carvedToTheEdge}) {
                            const float value = cloud::densityAt(position, *variant);
                            require(value <= base + 1.0e-6f,
                                "a detail octave must never add density");
                            if (base == 0.0f) {
                                require(value == 0.0f,
                                    "a detail octave must not create cloud where the base "
                                    "shape is empty");
                            }
                        }
                        if (base > 0.0f && cloud::densityAt(position, carved) == 0.0f) ++emptied;
                    }
                }
            }
            require(solid > 1000U, "the sweep must cross a substantial amount of cloud");
            require(emptied > 0U,
                "carving the interior must actually remove some of it, or the detail term is inert");
            std::cout << "  detail sweep: " << solid << " of " << (48 * 48 * 3)
                << " samples inside a cloud, " << emptied << " carved empty\n";
        }

        // 17. The weather map is inert when its variations are zero, and it is what it says it is
        //     when they are not. Both halves matter: the first makes the map an *addition* to C2's
        //     field rather than a rewrite of it, and it is exact rather than approximate because the
        //     map's contribution is multiplied by the variation.
        {
            atmosphere::AtmosphereParameters flat = parameters;
            flat.cloudCoverageVariation = 0.0f;
            flat.cloudTypeVariation = 0.0f;
            flat.cloudHeightVariation = 0.0f;
            atmosphere::AtmosphereParameters rescaled = flat;
            // Ten times the weather scale is a completely different map. With the variations at zero
            // the density must not move by a single bit, which is a stronger statement than "close".
            rescaled.cloudWeatherScale = flat.cloudWeatherScale * 10.0f;
            // `cloudType` is *not* inert at zero variation -- it is the layer-wide type, and the
            // profile is a continuous function of it. That is the point of separating the two.
            for (int step = 0; step < 400; ++step) {
                const glm::vec3 position(
                    -9000.0f + 47.0f * static_cast<float>(step),
                    4200.0f + 11.0f * static_cast<float>(step % 37),
                    -9000.0f + 90.0f * static_cast<float>(step));
                require(cloud::densityAt(position, flat) == cloud::densityAt(position, rescaled),
                    "a zero-contrast weather map must make the field independent of the map's scale");
            }

            // And with the coverage contrast on, the red channel's direction is the one it claims:
            // a sample in a high-coverage cell must gain density and one in a low-coverage cell must
            // lose it, relative to the same field with the map switched off.
            atmosphere::AtmosphereParameters mapped = parameters;
            mapped.cloudCoverageVariation = 0.9f;
            int thickened = 0;
            int thinned = 0;
            for (int step = 0; step < 2000; ++step) {
                const glm::vec3 position(
                    -20000.0f + 61.0f * static_cast<float>(step),
                    5600.0f,
                    -20000.0f + 137.0f * static_cast<float>(step % 211));
                const float coverageChannel = atmosphere::cloudWeather(
                    (position.x + mapped.cloudWindOffsetX) / mapped.cloudWeatherScale,
                    (position.z + mapped.cloudWindOffsetZ) / mapped.cloudWeatherScale, 0);
                const float withMap = cloud::densityAt(position, mapped);
                const float without = cloud::densityAt(position, flat);
                if (coverageChannel > 0.5f && withMap > without) ++thickened;
                if (coverageChannel < 0.5f && withMap < without) ++thinned;
                require((coverageChannel > 0.5f) ? withMap >= without - 1.0e-6f
                                                 : withMap <= without + 1.0e-6f,
                    "the weather map's red channel must thicken where it is high and thin where it "
                    "is low");
            }
            require(thickened > 20 && thinned > 20,
                "the weather sweep must find both a high-coverage cell and a low-coverage one");
            std::cout << "  weather sweep: " << thickened << " thickened, " << thinned
                << " thinned\n";
        }

        {
            auto p = parameters;
            p.enabled = p.cloudsEnabled = true;
            p.cloudCoverage = 0.65f;
            p.cloudFeatureScale = 1200;
            p.sunElevationDegrees = 45;
            const glm::vec3 sun = atmosphere::sunDirection(p);
            int dark = 0;
            for (int i = 0; i < 100; ++i) {
                const glm::vec3 receiver(i * 173.0f - 6000, 0, (i % 13) * 397.0f - 2500);
                const float t = cloud::shadowTransmittance(receiver, p, 48, cloud::volumetricExtinction);
                require(std::isfinite(t) && t >= 0 && t <= 1, "cloud shadow must conserve transmission");
                dark += t < 0.8f;
                require(std::abs(t - cloud::shadowTransmittance(receiver + sun * 20.0f, p,
                    48, cloud::volumetricExtinction)) < 1e-4f,
                    "all below-base receivers on one sun ray must share cloud transmission");
                auto shifted = p;
                shifted.cloudWindOffsetX += 350;
                require(std::abs(t - cloud::shadowTransmittance(receiver - glm::vec3(350,0,0),
                    shifted, 48, cloud::volumetricExtinction)) < 1e-4f,
                    "cloud shadow wind advection must match the shared density field");
                require(cloud::shadowTransmittance(receiver, p, 48, cloud::volumetricExtinction * 2) <= t + 1e-6f,
                    "increasing extinction must not brighten sun transmission");
            }
            require(dark > 10, "shadow tests must cross cloud bodies");
            p.sunElevationDegrees = -10;
            require(cloud::shadowTransmittance(glm::vec3(0), p, 48, 0.0025f) == 1,
                "sun below the horizon must not cast solar cloud shadows");
        }
        {
            auto reference=parameters;
            reference.enabled=reference.cloudsEnabled=true;
            atmosphere::applyCloudPreset(reference,atmosphere::CloudPreset::Cumulus);
            reference.cloudNoisePeriod=4;
            auto offline=reference;offline.cloudOfflineNoise=true;
            double squared=0;int overlap=0,covered=0,count=0;
            for(int z=0;z<24;++z)for(int x=0;x<32;++x)for(int y=0;y<12;++y) {
                const glm::vec3 position((x-16.0f)*reference.cloudFeatureScale/4.0f,
                    reference.cloudBaseHeight+(y+0.37f)/12.0f*(reference.cloudTopHeight-reference.cloudBaseHeight),
                    (z-12.0f)*reference.cloudFeatureScale/3.0f);
                const float a=cloud::densityAt(position,reference),b=cloud::densityAt(position,offline);
                const double delta=static_cast<double>(a)-b;squared+=delta*delta;++count;
                overlap+=(a>0.001f && b>0.001f);covered+=(a>0.001f || b>0.001f);
            }
            const double rmse=std::sqrt(squared/count);
            const double iou=static_cast<double>(overlap)/std::max(covered,1);
            require(covered>100 && rmse<0.015 && iou>0.90,
                "offline density must retain calibrated body shape and bounded approximation error");
            std::cout<<"  offline density RMSE "<<rmse<<", occupied IoU "<<iou<<'\n';
            require(!atmosphere::parametersMatch(reference,offline),"noise source must invalidate cloud history");
            require(atmosphere::environmentParametersMatch(reference,offline),"noise source must not rebake cloudless IBL");
        }
        std::cout << "Cloud reference raymarcher tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Cloud reference raymarcher tests failed: " << error.what() << '\n';
        return 1;
    }
}
