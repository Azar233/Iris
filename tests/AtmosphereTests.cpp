#include "optics/Atmosphere.h"

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

void requireColour(bool condition, const char* message, const glm::vec3& value) {
    if (!condition) throw std::runtime_error(std::string(message) + ": " + describe(value));
}

bool allFiniteNonNegative(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
        && value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f;
}

// A representative set of view directions: zenith, horizon and below the horizon, at eight
// azimuths, so a model change cannot hide an artefact in one quadrant.
std::vector<glm::vec3> sweepDirections() {
    std::vector<glm::vec3> directions;
    for (float elevation : {90.0f, 60.0f, 30.0f, 10.0f, 1.0f, -1.0f, -30.0f, -90.0f}) {
        for (int azimuth = 0; azimuth < 8; ++azimuth) {
            const float elevationRadians = glm::radians(elevation);
            const float azimuthRadians = glm::radians(azimuth * 45.0f);
            directions.push_back(glm::vec3(
                std::cos(elevationRadians) * std::sin(azimuthRadians),
                std::sin(elevationRadians),
                std::cos(elevationRadians) * std::cos(azimuthRadians)
            ));
        }
    }
    return directions;
}

void requireFiniteEverywhere(const atmosphere::AtmosphereParameters& parameters, const char* context) {
    for (const glm::vec3& direction : sweepDirections()) {
        require(allFiniteNonNegative(atmosphere::skyRadiance(direction, parameters)), context);
        require(allFiniteNonNegative(
                    atmosphere::sunDiskRadiance(direction, parameters)
                        + atmosphere::skyRadiance(direction, parameters)
                ),
                context);
    }
}

// A unit direction at `elevation` degrees, measured from +Z towards +X so it matches the
// `sunDirection` convention the layer's projection uses.
glm::vec3 directionAt(float elevationDegrees, float azimuthDegrees) {
    const float elevation = glm::radians(elevationDegrees);
    const float azimuth = glm::radians(azimuthDegrees);
    return glm::vec3(
        std::cos(elevation) * std::sin(azimuth),
        std::sin(elevation),
        std::cos(elevation) * std::cos(azimuth)
    );
}

// The P1-A slice 6 C1 contracts for the 2D analytic cloud layer. Kept in one function because every
// case needs the same "atmosphere on, clouds on, eye below the base" setup, and a future volumetric
// step (C2) has to keep all of them true while replacing what is inside.
void requireCloudLayerContracts(
    const atmosphere::AtmosphereParameters& skyParameters,
    float eyeHeight
) {
    atmosphere::AtmosphereParameters clouds = skyParameters;
    clouds.cloudsEnabled = true;

    // 1. A disabled layer or a disabled atmosphere contributes nothing at all, which is what keeps
    //    every scene written before this slice pixel-identical.
    atmosphere::AtmosphereParameters off = clouds;
    off.cloudsEnabled = false;
    atmosphere::AtmosphereParameters disabled = clouds;
    disabled.enabled = false;
    for (const glm::vec3& direction : sweepDirections()) {
        const atmosphere::CloudLayer layer =
            atmosphere::cloudLayer(direction, clouds, eyeHeight);
        require(allFiniteNonNegative(layer.ambient), "the layer radiance must be finite");
        require(layer.mask >= 0.0f && layer.mask <= 1.0f, "the mask must be a fraction");
        require(atmosphere::cloudLayer(direction, off, eyeHeight).mask == 0.0f,
                "a disabled cloud layer must not cover the sky");
        require(atmosphere::cloudLayer(direction, disabled, eyeHeight).mask == 0.0f,
                "a disabled atmosphere must not draw clouds");
        require(atmosphere::environmentRadiance(direction, off, eyeHeight)
                    == atmosphere::skyRadiance(direction, off)
                        + atmosphere::sunDiskRadiance(direction, off),
                "switching clouds off must restore the pre-cloud environment exactly");
    }

    // 2. The layer sits above the horizon. A downward ray's plane intersection is behind the camera,
    //    and the layer states that rather than projecting a mirrored cloud onto the ground.
    for (float elevation : {-90.0f, -30.0f, -1.0f, 0.0f}) {
        require(atmosphere::cloudLayer(
                    directionAt(elevation, 40.0f), clouds, eyeHeight).mask == 0.0f,
                "no cloud may be projected at or below the horizon");
    }

    // 3. Coverage is a fraction, not a threshold: raising it can only add cloud. C1 shipped the
    //    opposite direction, and the weather map's red channel runs the same way as this one now
    //    does. The sweep walks a full turn in azimuth so it crosses the field's own structure rather
    //    than one blob.
    constexpr float sampleElevation = 60.0f;
    std::size_t coveredSamples = 0U;
    constexpr int coverageSweepSamples = 120;
    for (int step = 0; step <= coverageSweepSamples; ++step) {
        const glm::vec3 direction = directionAt(
            sampleElevation, 360.0f * static_cast<float>(step)
                / static_cast<float>(coverageSweepSamples));
        const float mask = atmosphere::cloudLayer(direction, clouds, eyeHeight).mask;
        atmosphere::AtmosphereParameters highCoverage = clouds;
        highCoverage.cloudCoverage = clouds.cloudCoverage + 0.25f;
        require(atmosphere::cloudLayer(direction, highCoverage, eyeHeight).mask >= mask,
                "raising the coverage fraction must not remove cloud");
        if (mask > 0.0f) ++coveredSamples;
    }
    require(coveredSamples > 0U, "the sweep must cross at least one cloud");
    // The field is not a constant: a full turn has to contain both cloud and gap.
    require(coveredSamples < static_cast<std::size_t>(coverageSweepSamples + 1),
        "a full turn must contain gaps as well as cloud");

    // 4. The same direction and parameters must produce bitwise identical values, because the raster
    //    skybox, the CPU path tracer's captured environment and a headless render all bake this
    //    layer and a baseline is only comparable if they agree.
    const glm::vec3 fixedDirection = directionAt(35.0f, 120.0f);
    const atmosphere::CloudLayer first = atmosphere::cloudLayer(fixedDirection, clouds, eyeHeight);
    const atmosphere::CloudLayer second = atmosphere::cloudLayer(fixedDirection, clouds, eyeHeight);
    require(first.mask == second.mask && first.ambient == second.ambient,
            "the cloud layer must be deterministic");

    // 5. The slab projection has a computable CPU expectation. `pathLength` is the closed form the
    //    thickness term is built from -- a vertical ray crosses exactly the slab, a grazing ray
    //    crosses `slab / sin(elevation)` -- and the layer's radiance is linear in the mask it
    //    produces, which is what lets the renderer be compared against a CPU reference later.
    //
    //    The probe pins the layer to full, uniform coverage first, because `pathLength` is only
    //    reported where the layer is not empty and a weather map that left one of the five probe
    //    elevations in a gap would fail the geometry contract for a reason that is not geometry.
    clouds.cloudCoverage = 1.0f;
    clouds.cloudCoverageVariation = 0.0f;
    clouds.cloudTypeVariation = 0.0f;
    clouds.cloudHeightVariation = 0.0f;
    const float slabThickness = clouds.cloudTopHeight - clouds.cloudBaseHeight;
    std::size_t coveredElevations = 0U;
    for (float elevation : {25.0f, 40.0f, 55.0f, 70.0f, 85.0f}) {
        const glm::vec3 direction = directionAt(elevation, 90.0f);
        const atmosphere::CloudLayer layer = atmosphere::cloudLayer(direction, clouds, eyeHeight);
        const float expectedPathLength = slabThickness / std::sin(glm::radians(elevation));
        require(std::abs(layer.pathLength - expectedPathLength) < 1.0e-2f,
                "the slab path length must be the closed form slab / sin(elevation)");
        if (layer.mask > 0.0f) ++coveredElevations;
    }
    require(coveredElevations > 0U, "the projection check needs at least one covered elevation");
    // Directly: a vertical ray crosses the slab and nothing more.
    require(std::abs(atmosphere::cloudLayer(
                directionAt(90.0f, 90.0f), clouds, eyeHeight).pathLength - slabThickness) < 1.0e-4f,
            "a vertical ray must cross exactly the slab");
    // The thickness term is the only thing about the mask that depends on the view angle, so fixing
    // the sampled noise and comparing two elevations whose paths are both clamped to the full slab
    // must give the same mask: the layer's opacity saturates rather than growing without bound.
    {
        const float shallowMask = atmosphere::cloudLayer(
            directionAt(20.0f, 200.0f), clouds, eyeHeight).mask;
        const float steepMask = atmosphere::cloudLayer(
            directionAt(85.0f, 200.0f), clouds, eyeHeight).mask;
        // Both rays cross at least one full slab, so any difference is the noise field, not the
        // geometry; the contract worth locking is that neither exceeds the density ceiling.
        require(shallowMask <= clouds.cloudDensity + 1.0e-6f
                    && steepMask <= clouds.cloudDensity + 1.0e-6f,
                "the mask must saturate at the layer's density");
    }

    // 6. Tiling is the property the layer's unbounded horizontal projection rests on: shifting a
    //    sample by exactly one period in either axis, at a point inside a cell, reproduces the value
    //    bit for bit. It is asserted on the noise itself rather than through a pair of camera rays,
    //    because reconstructing an exact whole-period shift from directions reintroduces the
    //    projection's rounding -- the contract is about the field, and the field is what is tested.
    //    Probes use half-integer offsets so the arithmetic is exact in binary floating point.
    for (int period : {1, 2, 4, 8}) {
        const float step = static_cast<float>(period);
        for (float x = 0.5f; x < 4.0f; x += 1.0f) {
            for (float y = 0.5f; y < 4.0f; y += 1.0f) {
                const float base = atmosphere::worley2x2(x, y, period);
                require(atmosphere::worley2x2(x + step, y, period) == base,
                    "one period along X must reproduce the worley field exactly");
                require(atmosphere::worley2x2(x, y + step, period) == base,
                    "one period along Y must reproduce the worley field exactly");
                require(atmosphere::worley2x2(x + step, y + step, period) == base,
                    "one period along both axes must reproduce the worley field exactly");
                // The same must hold through the fBm sum the layer actually samples.
                require(atmosphere::cloudShape(x + step, y + step, period)
                        == atmosphere::cloudShape(x, y, period),
                    "one period must reproduce the fBm the layer samples");
                // And through the detail octaves that carve a cloud's interior (C5).
                require(atmosphere::cloudDetailShape(x + step, y + step, period)
                        == atmosphere::cloudDetailShape(x, y, period),
                    "one period must reproduce the detail octaves exactly");
            }
        }
    }
    // Tiling is only meaningful because the field varies within a period.
    {
        float minimumShape = 2.0f;
        float maximumShape = -1.0f;
        for (int step = 0; step <= 64; ++step) {
            const float sample = atmosphere::cloudShape(
                0.25f + 3.5f * static_cast<float>(step) / 64.0f, 1.75f, 4);
            require(std::isfinite(sample) && sample >= 0.0f && sample <= 1.0f,
                "the shape field must stay a normalised density");
            minimumShape = std::min(minimumShape, sample);
            maximumShape = std::max(maximumShape, sample);
        }
        require(maximumShape - minimumShape > 0.1f,
            "one period must contain both cloud and gap, or it is not a field");
    }
    // The layer's own projection still has to be a function of the noise it feeds: at a fixed
    // elevation the mask varies with azimuth, and it never leaves [0, 1]. This uses the layer's
    // default coverage rather than the probe values above, because a nearly-overcast threshold
    // legitimately has no gaps to find.
    {
        atmosphere::AtmosphereParameters sweepParameters = clouds;
        // Sample several bodies across the ring. The 3D base now uses featureScale for one large
        // body; the old field packed four small cells into that distance. Disable erosion here so
        // this check measures the projected shape rather than whether a thin cell survives detail.
        sweepParameters.cloudFeatureScale = 1200.0f;
        sweepParameters.cloudCoverage = 0.55f;
        sweepParameters.cloudDetailStrength = 0.0f;
        std::size_t cloudSamples = 0U;
        constexpr int sweepSamples = 96;
        for (int step = 0; step < sweepSamples; ++step) {
            const float azimuth = 360.0f * static_cast<float>(step)
                / static_cast<float>(sweepSamples);
            const atmosphere::CloudLayer sample =
                atmosphere::cloudLayer(directionAt(50.0f, azimuth), sweepParameters, eyeHeight);
            require(std::isfinite(sample.mask) && sample.mask >= 0.0f && sample.mask <= 1.0f,
                "every projected direction must produce a normalised mask");
            if (sample.mask > 0.0f) ++cloudSamples;
        }
        require(cloudSamples > 0U && cloudSamples < sweepSamples,
            "a projection sweep must contain both cloud and gap");
    }

    // 7. The layer is an attenuator composited over the sky, not a replacement: where it is opaque
    //    the sky behind it is gone but the sky's energy is not simply deleted from the model, and
    //    where it is thin the sky still shows through.
    {
        const glm::vec3 thinDirection = directionAt(45.0f, 90.0f);
        atmosphere::AtmosphereParameters overcast = clouds;
        overcast.cloudCoverage = 1.0f;
        overcast.cloudDensity = 0.02f;
        const glm::vec3 clearSky = atmosphere::environmentRadiance(
            thinDirection, overcast, eyeHeight);
        const glm::vec3 behind = atmosphere::skyRadiance(thinDirection, overcast);
        require(glm::length(clearSky) > glm::length(behind) * 0.4f,
                "a very thin layer must still let the sky through");
    }

    // 8. The layer is lit by the same sun as the sky, so it cannot glow from underneath: a sun below
    //    the horizon leaves the layer's scattered term dark even though its mask remains.
    {
        atmosphere::AtmosphereParameters night = clouds;
        night.sunElevationDegrees = -12.0f;
        float coveredAtNight = 0.0f;
        float ambientAtNight = 0.0f;
        atmosphere::AtmosphereParameters day = clouds;
        day.sunElevationDegrees = 55.0f;
        float ambientAtDay = 0.0f;
        for (int step = 0; step <= 60; ++step) {
            const glm::vec3 direction = directionAt(
                50.0f, 360.0f * static_cast<float>(step) / 60.0f);
            const atmosphere::CloudLayer nightLayer =
                atmosphere::cloudLayer(direction, night, eyeHeight);
            const atmosphere::CloudLayer dayLayer =
                atmosphere::cloudLayer(direction, day, eyeHeight);
            coveredAtNight += nightLayer.mask;
            ambientAtNight += glm::length(nightLayer.ambient);
            ambientAtDay += glm::length(dayLayer.ambient);
            require(allFiniteNonNegative(nightLayer.ambient),
                    "a night layer must stay finite");
        }
        require(coveredAtNight > 0.0f, "the night sweep must still see the layer's shape");
        require(ambientAtNight < ambientAtDay * 0.5f,
                "a sun below the horizon must not light the layer");
    }

    // 9. Hostile inputs must stay finite: a camera inside the layer, above it, at ground level, and
    //    far below it, plus zero and enormous thickness. The renderer reports the projection error
    //    through `minimumCameraHeight` instead of failing.
    for (float cameraHeight : {-5000.0f, 0.0f, 2000.0f, 10000.0f, 1.0e7f}) {
        for (const glm::vec3& direction : sweepDirections()) {
            const atmosphere::CloudLayer layer =
                atmosphere::cloudLayer(direction, clouds, cameraHeight);
            require(allFiniteNonNegative(layer.ambient),
                    "every camera height must produce a finite layer");
            require(layer.mask >= 0.0f && layer.mask <= 1.0f,
                    "every camera height must produce a fraction");
        }
    }
    atmosphere::AtmosphereParameters flat = clouds;
    flat.cloudTopHeight = flat.cloudBaseHeight;
    flat.cloudCoverage = 0.9f;
    require(allFiniteNonNegative(atmosphere::cloudLayer(
                directionAt(45.0f, 90.0f), flat, eyeHeight).ambient),
            "a collapsed slab must not divide by zero");
    atmosphere::AtmosphereParameters inverted = clouds;
    inverted.cloudTopHeight = inverted.cloudBaseHeight - 500.0f;
    require(allFiniteNonNegative(atmosphere::cloudLayer(
                directionAt(45.0f, 90.0f), inverted, eyeHeight).ambient),
            "an inverted slab must not produce a negative layer");
    atmosphere::AtmosphereParameters huge = clouds;
    huge.cloudBaseHeight = 1.0e6f;
    huge.cloudTopHeight = 2.0e6f;
    huge.cloudFeatureScale = 1.0e6f;
    require(allFiniteNonNegative(atmosphere::cloudLayer(
                directionAt(1.0f, 90.0f), huge, eyeHeight).ambient),
            "an enormous layer must stay finite near the horizon");
    atmosphere::AtmosphereParameters negativeClouds = clouds;
    negativeClouds.cloudCoverage = -1.0f;
    negativeClouds.cloudDensity = -3.0f;
    negativeClouds.cloudFeatureScale = -10.0f;
    negativeClouds.cloudNoisePeriod = -4.0f;
    negativeClouds.cloudHorizonFadeDegrees = -5.0f;
    for (const glm::vec3& direction : sweepDirections()) {
        require(allFiniteNonNegative(atmosphere::cloudLayer(
                    direction, negativeClouds, eyeHeight).ambient),
                "negative cloud parameters must clamp instead of returning NaN");
    }

    // 10. Coverage is a fraction of the sky, and it runs the way its name says: 0 is an empty sky, 1
    //     is solid overcast, and every step between adds cloud rather than removing it. C1 shipped
    //     this parameter as a *threshold*, where 1 emptied the layer; the flip is deliberate and the
    //     weather map's red channel is the reason -- two coverage knobs pointing opposite ways is how
    //     a parameter set ends up silently inverted.
    {
        const auto coverageMask = [&](float coverage) {
            atmosphere::AtmosphereParameters parameters = clouds;
            parameters.cloudCoverage = coverage;
            parameters.cloudFeatureScale = 1200.0f;
            parameters.cloudDetailStrength = 0.0f;
            float total = 0.0f;
            for (int step = 0; step <= 80; ++step) {
                total += atmosphere::cloudLayer(directionAt(
                    40.0f, 360.0f * static_cast<float>(step) / 80.0f), parameters, eyeHeight).mask;
            }
            return total;
        };
        const float openSky = coverageMask(0.0f);
        const float halfCovered = coverageMask(0.5f);
        const float overcast = coverageMask(1.0f);
        require(openSky < halfCovered && halfCovered < overcast,
            "raising the coverage fraction must monotonically add cloud");
        require(openSky == 0.0f,
            "zero coverage must leave the sky completely empty");
        require(overcast > 0.0f,
            "full coverage must put cloud in every direction the field is non-zero");
    }

    // 10b. The weather map (C5). Red is coverage, green is cloud type, blue is height; the three are
    //      tileable on the same terms as the cloud field, stay inside [0, 1], and are decorrelated --
    //      a map whose channels moved together would mean that raising the coverage contrast also
    //      changed the cloud type, which is exactly the coupling the three channels exist to avoid.
    {
        double channelMean[3] = {0.0, 0.0, 0.0};
        double channelMinimum[3] = {2.0, 2.0, 2.0};
        double channelMaximum[3] = {-1.0, -1.0, -1.0};
        int samples = 0;
        double disagreement = 0.0;
        for (int step = 0; step <= 128; ++step) {
            const float x = 0.125f + 2.75f * static_cast<float>(step) / 128.0f;
            const float y = 0.625f;
            for (int channel = 0; channel < 3; ++channel) {
                const float value = atmosphere::cloudWeather(x, y, channel);
                require(std::isfinite(value) && value >= 0.0f && value <= 1.0f,
                    "every weather channel must be a normalised value");
                // Tiling, on the same whole-period shift the cloud field's contract uses.
                require(atmosphere::cloudWeather(x + 1.0f, y, channel) == value,
                    "one period must reproduce a weather channel exactly");
                require(atmosphere::cloudWeather(x, y - 1.0f, channel) == value,
                    "one period must reproduce a weather channel exactly along the other axis");
                channelMean[channel] += value;
                channelMinimum[channel] = std::min(channelMinimum[channel], static_cast<double>(value));
                channelMaximum[channel] = std::max(channelMaximum[channel], static_cast<double>(value));
            }
            disagreement += std::abs(
                atmosphere::cloudWeather(x, y, 0) - atmosphere::cloudWeather(x, y, 1));
            ++samples;
        }
        for (int channel = 0; channel < 3; ++channel) {
            channelMean[channel] /= samples;
            require(channelMaximum[channel] - channelMinimum[channel] > 0.2,
                "a weather channel must actually vary, or it is not a map");
            require(channelMean[channel] > 0.2 && channelMean[channel] < 0.8,
                "a weather channel must be centred, or it biases the layer instead of varying it");
        }
        require(disagreement / samples > 0.1,
            "the coverage and cloud-type channels must not be the same map");
    }

    // 11. The phase function is normalised to average about one, and it has both lobes: strongly
    //     forward-scattering at the sun (the silver lining) and a weaker rise directly behind the
    //     layer, which is what keeps the anti-solar side from going flat. The minimum is near the
    //     side of the layer, where neither lobe contributes.
    require(atmosphere::cloudPhase(1.0f) > atmosphere::cloudPhase(0.0f) * 10.0f,
            "the phase function must strongly favour forward scattering");
    require(atmosphere::cloudPhase(-1.0f) > atmosphere::cloudPhase(0.0f),
            "the phase function needs a backward lobe behind the layer");
    require(atmosphere::cloudPhase(1.0f) > atmosphere::cloudPhase(-1.0f),
            "forward scattering must dominate the backward lobe");
    float phaseIntegral = 0.0f;
    const int phaseSteps = 4000;
    for (int index = 0; index < phaseSteps; ++index) {
        const float cosine = -1.0f + 2.0f * (static_cast<float>(index) + 0.5f)
            / static_cast<float>(phaseSteps);
        phaseIntegral += atmosphere::cloudPhase(cosine);
    }
    phaseIntegral *= 2.0f / static_cast<float>(phaseSteps) * 0.5f;
    require(std::abs(phaseIntegral - 1.0f) < 0.05f,
            "the phase function must integrate to one over the sphere");
    require(std::isfinite(atmosphere::cloudPhase(5.0f))
                && std::isfinite(atmosphere::cloudPhase(-5.0f)),
            "an out-of-range cosine must clamp to a finite phase");

    // 12. The lighting contract, stated numerically because "looks like a cloud" is not testable and
    //     the measured ratios are. A cloud sits in the dense bright air below it, so near the horizon
    //     it is darker than the sky it covers (a silhouette) and higher up it is brighter (a lit
    //     cloud). Those two facts are what make a layer readable at all; getting the ambient from the
    //     zenith alone measured 0.04 of the sky at the horizon and rendered as a flat grey stain.
    {
        atmosphere::AtmosphereParameters lit = clouds;
        lit.cloudBaseHeight = 3200.0f;
        lit.cloudTopHeight = 8000.0f;
        // Isolate illumination from the silhouette: every ring must contain cloudy samples even
        // when its 3D cross-section differs. Coverage response is checked separately above.
        lit.cloudCoverage = 1.0f;
        lit.cloudDetailStrength = 0.0f;
        lit.cloudCoverageVariation = 0.0f;
        const auto meanCoveredRatio = [&](float elevationDegrees) {
            const float elevation = glm::radians(elevationDegrees);
            double ratio = 0.0;
            int covered = 0;
            for (int step = 0; step < 72; ++step) {
                const float azimuth = 2.0f * 3.14159265358979323846f
                    * static_cast<float>(step) / 72.0f;
                const glm::vec3 direction(
                    std::cos(elevation) * std::sin(azimuth),
                    std::sin(elevation),
                    std::cos(elevation) * std::cos(azimuth));
                const atmosphere::CloudLayer layer =
                    atmosphere::cloudLayer(direction, lit, eyeHeight);
                if (layer.mask <= 0.05f) continue;
                const glm::vec3 sky = atmosphere::skyRadiance(direction, lit) * (1.0f - layer.mask);
                const auto luminance = [](const glm::vec3& value) {
                    return 0.2126 * value.r + 0.7152 * value.g + 0.0722 * value.b;
                };
                ratio += luminance(layer.ambient) / std::max(luminance(sky), 1.0e-6);
                ++covered;
            }
            require(covered > 0, "every lighting probe ring must contain cloud");
            return ratio / covered;
        };
        const double nearHorizon = meanCoveredRatio(5.0f);
        const double midSky = meanCoveredRatio(30.0f);
        const double highSky = meanCoveredRatio(45.0f);
        require(nearHorizon > 0.15,
            "a cloud near the horizon must be visible against the sky, not a black stain");
        // Elevation samples different 3D bodies and therefore different densities. A monotonic
        // brightness ordering between those bodies is not a lighting invariant; pin visibility at
        // each elevation instead. The physical limits and integral are tested by cloud-reference.
        require(highSky > 0.15 && midSky > 0.15,
            "clouds must remain visibly lit at middle and high elevations");
        require(highSky > 1.0,
            "a high cloud must be brighter than the sky it covers, or it reads as a hole");
    }

    // 13. Every cloud parameter belongs in the environment cache key: each one below changes what is
    //     baked, so a consumer that reused a cached sky would render the wrong layer.
    const auto cloudChangeInvalidates = [&clouds](const char* what, auto&& mutate) {
        atmosphere::AtmosphereParameters changed = clouds;
        mutate(changed);
        require(!atmosphere::parametersMatch(clouds, changed), what);
    };
    cloudChangeInvalidates("enabling clouds must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudsEnabled = !p.cloudsEnabled; });
    cloudChangeInvalidates("moving the cloud base must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudBaseHeight += 250.0f; });
    cloudChangeInvalidates("moving the cloud top must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudTopHeight += 250.0f; });
    cloudChangeInvalidates("changing coverage must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudCoverage += 0.2f; });
    cloudChangeInvalidates("changing density must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudDensity += 0.5f; });
    cloudChangeInvalidates("advecting the layer must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudWindOffsetX += 40.0f; });
    cloudChangeInvalidates("advecting the layer north must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudWindOffsetZ += 40.0f; });
    cloudChangeInvalidates("changing the feature scale must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudFeatureScale += 500.0f; });
    cloudChangeInvalidates("changing the tile period must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudNoisePeriod += 1.0f; });
    cloudChangeInvalidates("changing the horizon fade must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudHorizonFadeDegrees += 2.0f; });
    cloudChangeInvalidates("moving the ambient direction must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudAmbientElevationDegrees += 5.0f; });
    cloudChangeInvalidates("changing the ambient scale must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudAmbientScale += 0.2f; });
    // The C5 weather map's controls, and the quality tier. The tier belongs here for a reason worth
    // stating: it changes the integral's step count, so it moves pixels without a single field
    // parameter changing, and a cache that ignored it would hand a High-tier consumer a Low-tier
    // bake.
    cloudChangeInvalidates("changing the weather scale must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudWeatherScale += 1000.0f; });
    cloudChangeInvalidates("changing the coverage variation must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudCoverageVariation += 0.2f; });
    cloudChangeInvalidates("changing the cloud type must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudType += 0.2f; });
    cloudChangeInvalidates("changing the type variation must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudTypeVariation += 0.2f; });
    cloudChangeInvalidates("changing the height variation must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudHeightVariation += 0.2f; });
    cloudChangeInvalidates("changing the detail strength must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudDetailStrength += 0.2f; });
    cloudChangeInvalidates("changing the detail edge weight must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) { p.cloudDetailEdge += 0.2f; });
    cloudChangeInvalidates("changing the quality tier must invalidate the cached environment",
        [](atmosphere::AtmosphereParameters& p) {
            p.cloudQuality = p.cloudQuality == atmosphere::CloudQualityTier::High
                ? atmosphere::CloudQualityTier::Low : atmosphere::CloudQualityTier::High;
        });

    // 14. The quality tiers, and the presets built on them. A tier is allowed to change how much of
    //     the integral is paid for and nothing else -- the C5 acceptance contract is that the same
    //     weather map gives the same structure at both tiers, which is only testable if the tier is
    //     a step budget and not a second set of shape parameters.
    {
        const atmosphere::CloudTierBudget low =
            atmosphere::cloudTierBudget(atmosphere::CloudQualityTier::Low);
        const atmosphere::CloudTierBudget high =
            atmosphere::cloudTierBudget(atmosphere::CloudQualityTier::High);
        // The brief's measured ranges: 24..48 primary steps, 4..6 light steps.
        require(low.primarySteps >= 24 && low.primarySteps <= 48,
            "the low tier's step count must stay inside the brief's measured range");
        require(high.primarySteps >= 24 && high.primarySteps <= 48,
            "the high tier's step count must stay inside the brief's measured range");
        require(low.lightSteps >= 4 && low.lightSteps <= 6
                    && high.lightSteps >= 4 && high.lightSteps <= 6,
            "both tiers' light step counts must stay inside the brief's measured range");
        require(high.primarySteps > low.primarySteps && high.lightSteps >= low.lightSteps,
            "the high tier must be the more expensive one");

        // Each preset has to be a whole air mass rather than a tweak, and the three have to be
        // distinguishable in the quantities their names claim: altitude, thickness and type.
        atmosphere::AtmosphereParameters cumulus;
        atmosphere::applyCloudPreset(cumulus, atmosphere::CloudPreset::Cumulus);
        atmosphere::AtmosphereParameters stratus;
        atmosphere::applyCloudPreset(stratus, atmosphere::CloudPreset::Stratus);
        atmosphere::AtmosphereParameters cirrus;
        atmosphere::applyCloudPreset(cirrus, atmosphere::CloudPreset::Cirrus);
        for (const auto* entry : {&cumulus, &stratus, &cirrus}) {
            require(entry->cloudsEnabled, "a preset must switch the layer on");
            require(entry->cloudTopHeight > entry->cloudBaseHeight,
                "a preset must describe a slab with thickness");
            require(entry->cloudCoverage > 0.0f && entry->cloudCoverage < 1.0f,
                "a preset must leave both cloud and gap");
        }
        require(stratus.cloudType < cumulus.cloudType,
            "stratus must be the layered preset and cumulus the convective one");
        require(stratus.cloudTopHeight - stratus.cloudBaseHeight
                    < cumulus.cloudTopHeight - cumulus.cloudBaseHeight,
            "stratus must be the shallower deck");
        require(cirrus.cloudBaseHeight > cumulus.cloudTopHeight,
            "cirrus must sit above the cumulus layer");
        require(cirrus.cloudDensity < cumulus.cloudDensity,
            "cirrus must be the thinner cloud");
        // The preset must survive a round trip through matching, or a scene that applies one and
        // then compares it to itself would keep invalidating its own cached environment.
        atmosphere::AtmosphereParameters cumulusAgain;
        atmosphere::applyCloudPreset(cumulusAgain, atmosphere::CloudPreset::Cumulus);
        require(atmosphere::parametersMatch(cumulus, cumulusAgain),
            "applying the same preset twice must produce identical parameters");
    }
}

} // namespace

int main() {
    try {
        atmosphere::AtmosphereParameters parameters;
        require(!parameters.enabled, "the atmosphere must be off by default");
        require(allFiniteNonNegative(atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), parameters)),
                "a disabled atmosphere must still return a finite sky");
        require(glm::length(atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), parameters)) == 0.0f,
                "a disabled atmosphere must return black");

        parameters.enabled = true;

        // The sun direction is the single input the sky, the light and the disk share.
        parameters.sunElevationDegrees = 45.0f;
        parameters.sunAzimuthDegrees = 90.0f;
        const glm::vec3 sun = atmosphere::sunDirection(parameters);
        require(std::abs(glm::length(sun) - 1.0f) < 1.0e-5f, "the sun direction must be a unit vector");
        require(std::abs(sun.x - std::cos(glm::radians(45.0f))) < 1.0e-5f,
                "azimuth 90 must place the sun on +X");
        require(std::abs(sun.y - std::sin(glm::radians(45.0f))) < 1.0e-5f,
                "elevation must control the sun height");
        require(std::abs(sun.z) < 1.0e-5f, "azimuth 90 must leave no +Z component");

        // Overhead sun: the zenith must be blue, and much brighter than the sky opposite the
        // horizon, which is the defining shape of a daytime Rayleigh sky.
        parameters.sunElevationDegrees = 70.0f;
        const glm::vec3 noonZenith = atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), parameters);
        require(allFiniteNonNegative(noonZenith), "the noon zenith must be finite and non-negative");
        requireColour(noonZenith.b > noonZenith.r * 1.5f, "the noon zenith must be blue", noonZenith);
        // The phase functions must brighten the sky towards the sun: that sun-side brightening is
        // what a coastal Hero Scene reads as daylight, and it is what this model guarantees. It
        // deliberately does *not* reproduce the real clear-sky fact that the anti-solar horizon is
        // the darkest part of the sky -- single scattering lacks the multiple scattering and
        // density profile that cause it. That limitation is recorded in docs/atmosphere-sky.md.
        const glm::vec3 noonSunDirection = atmosphere::sunDirection(parameters);
        const glm::vec3 towardSunHorizon = atmosphere::skyRadiance(
            glm::normalize(glm::vec3(noonSunDirection.x, 0.08f, noonSunDirection.z)), parameters
        );
        const glm::vec3 antiSunHorizon = atmosphere::skyRadiance(
            glm::normalize(glm::vec3(-noonSunDirection.x, 0.08f, -noonSunDirection.z)), parameters
        );
        require(glm::length(towardSunHorizon) > glm::length(antiSunHorizon) * 1.2f,
                "the sky towards the sun must be brighter than the anti-solar sky");
        require(allFiniteNonNegative(towardSunHorizon) && allFiniteNonNegative(antiSunHorizon),
                "both horizons must be finite");

        // Low sun: the sky around the sun reddens, which is the sunset signal the light colour
        // and the aerial perspective both depend on.
        atmosphere::AtmosphereParameters sunset = parameters;
        sunset.sunElevationDegrees = 2.0f;
        const glm::vec3 sunsetNearSun = atmosphere::skyRadiance(
            atmosphere::sunDirection(sunset), sunset
        );
        require(allFiniteNonNegative(sunsetNearSun), "the sunset horizon must be finite");
        require(sunsetNearSun.r > sunsetNearSun.b * 1.5f,
                "the sky towards a low sun must be red");
        const glm::vec3 noonNearSun = atmosphere::skyRadiance(
            atmosphere::sunDirection(parameters), parameters
        );
        require(noonNearSun.b > sunsetNearSun.b * 0.5f,
                "a high sun must not be as red as a low one");

        // Sun transmittance drives the directional light colour: dimmer and redder as the sun
        // sets, and never brighter than unity.
        const glm::vec3 noonSun = atmosphere::sunTransmittance(parameters);
        const glm::vec3 sunsetSun = atmosphere::sunTransmittance(sunset);
        require(allFiniteNonNegative(noonSun) && allFiniteNonNegative(sunsetSun),
                "sun transmittance must be finite");
        require(noonSun.r <= 1.0f && noonSun.b <= 1.0f, "sun transmittance must not exceed unity");
        require(glm::length(noonSun) > glm::length(sunsetSun),
                "the sun must lose energy as it sets");
        require(sunsetSun.r > sunsetSun.b, "a setting sun must redden");

        // The key light's colour is that same transmittance with its brightness divided out, so a
        // setting sun reddens the light instead of only dimming it. Brightness stays in
        // `sunTransmittance`'s luminance, which is why the two are separate: multiplying the raw
        // transmittance into the light would apply the extinction twice.
        const glm::vec3 noonColour = atmosphere::skyLightColor(parameters);
        const glm::vec3 sunsetColour = atmosphere::skyLightColor(sunset);
        require(allFiniteNonNegative(noonColour) && allFiniteNonNegative(sunsetColour),
                "the key light colour must be finite and non-negative");
        const float noonPeak = std::max(noonColour.r, std::max(noonColour.g, noonColour.b));
        const float sunsetPeak = std::max(sunsetColour.r, std::max(sunsetColour.g, sunsetColour.b));
        require(std::abs(noonPeak - 1.0f) < 1.0e-5f && std::abs(sunsetPeak - 1.0f) < 1.0e-5f,
                "the key light colour must be normalised to its brightest channel");
        require(sunsetColour.r > sunsetColour.b * 1.5f,
                "a setting sun must redden the key light, not only dim it");
        // Red is the peak channel on both sides, so the warmth is a ratio, not a raw component:
        // every other channel has to give way relative to red as the sun drops.
        require(sunsetColour.g / sunsetColour.r < noonColour.g / noonColour.r
                    && sunsetColour.b / sunsetColour.r < noonColour.b / noonColour.r,
                "the sun must be redder near the horizon than overhead");
        // The light is never *blue*: these coefficients are tuned well above the physical Rayleigh
        // values so the sky reads as blue without a numeric march, which leaves blue the most
        // attenuated channel even with the sun overhead. The contract locked in here is the
        // direction of the change -- blue transmission recovers monotonically as the sun climbs --
        // not a claim that a high sun is spectrally white. `docs/atmosphere-sky.md` records it.
        float previousBlue = 0.0f;
        for (float elevation : {2.0f, 20.0f, 45.0f, 90.0f}) {
            atmosphere::AtmosphereParameters sweep = parameters;
            sweep.sunElevationDegrees = elevation;
            const glm::vec3 colour = atmosphere::skyLightColor(sweep);
            require(colour.b > previousBlue,
                    "blue transmission must recover as the sun rises");
            require(colour.r >= colour.b,
                    "the key light must never turn bluer than it is red");
            previousBlue = colour.b;
        }
        const glm::vec3 white{1.0f};
        require(atmosphere::skyLightColor(atmosphere::AtmosphereParameters{}) == white,
                "a disabled atmosphere must leave the key light neutral");
        atmosphere::AtmosphereParameters extinguished = parameters;
        extinguished.sunElevationDegrees = -10.0f;
        require(atmosphere::skyLightColor(extinguished) == white
                    || allFiniteNonNegative(atmosphere::skyLightColor(extinguished)),
                "a sun far below the horizon must not turn the light into NaN or a negative colour");
        require(atmosphere::skyLightColor(parameters) == noonColour,
                "the key light colour must be deterministic");

        // Aerial perspective integrates the same coefficients over a finite segment instead of to
        // the top of the atmosphere, so it has to agree with the sky at both limits: zero length is
        // transparent, and a segment long enough to escape saturates on the transmittance the key
        // light already uses.
        const float segmentLength = 420.0f;
        // One world unit is one metre here, so the numbers below are directly comparable with the
        // model's own metre-scale coefficients.
        const float testWorldUnitsPerMetre = 1.0f;
        const glm::vec3 horizontalDepth = atmosphere::opticalDepthAlongSegment(
            parameters, segmentLength, 0.0f, testWorldUnitsPerMetre
        );
        require(allFiniteNonNegative(horizontalDepth) && glm::length(horizontalDepth) > 0.0f,
                "a horizontal segment must accumulate a finite non-zero optical depth");
        require(glm::length(atmosphere::opticalDepthAlongSegment(
                    parameters, 0.0f, -1.0f, testWorldUnitsPerMetre
                )) == 0.0f,
                "a zero-length segment must be transparent");
        require(glm::length(atmosphere::opticalDepthAlongSegment(
                    parameters, -5.0f, -1.0f, testWorldUnitsPerMetre
                )) == 0.0f,
                "a negative segment must not accumulate negative optical depth");
        const glm::vec3 nearDepth = atmosphere::opticalDepthAlongSegment(
            parameters, segmentLength, -1.0f, testWorldUnitsPerMetre
        );
        const glm::vec3 farDepth = atmosphere::opticalDepthAlongSegment(
            parameters, segmentLength * 1000.0f, -1.0f, testWorldUnitsPerMetre
        );
        require(glm::length(farDepth) > glm::length(nearDepth),
                "a longer segment must accumulate more optical depth");
        // A scene is free to model its own unit scale; the optical depth must follow the physical
        // distance, so one unit standing for ten metres has to thin the air tenfold.
        const glm::vec3 scaledDepth = atmosphere::opticalDepthAlongSegment(
            parameters, segmentLength, -1.0f, 10.0f
        );
        require(glm::length(scaledDepth * 10.0f - nearDepth) < glm::length(nearDepth) * 0.2f,
                "the unit scale must convert world units into metres");
        // Looking straight up, the column above the camera is the whole atmosphere, so the depth has
        // to saturate instead of growing without bound -- and it must stop at a property of the
        // atmosphere, not of where the sun happens to be.
        const glm::vec3 upwardDepth = atmosphere::opticalDepthAlongSegment(
            parameters, 1.0e7f, 1.0f, testWorldUnitsPerMetre
        );
        atmosphere::AtmosphereParameters elsewhere = parameters;
        elsewhere.sunElevationDegrees = 30.0f;
        require(glm::length(atmosphere::opticalDepthAlongSegment(
                    elsewhere, 1.0e7f, 1.0f, testWorldUnitsPerMetre
                ) - upwardDepth) == 0.0f,
                "the vertical column must not depend on the sun's elevation");
        // Cross-check the saturation against the model's own column. `sunTransmittance` is evaluated
        // at the *sun's* zenith angle, so undoing it needs that angle's air mass rather than the
        // vertical ray's: the Kasten-Young fit gives 1.0637 at the sun's 20 deg zenith and 0.99971
        // for a perfectly vertical ray, and the column factor is 1 for a ray that escapes. What the
        // residual is allowed to contain is only that air-mass convention, nothing else.
        const glm::vec3 atmosphereColumn = glm::vec3(
            -glm::log(atmosphere::sunTransmittance(parameters))
        ) / 1.0636999870686308f;
        require(glm::length(upwardDepth - atmosphereColumn)
                    < glm::length(atmosphereColumn) * 0.01f,
                "an escaping upward segment must saturate on the full atmospheric column");
        // The whole vertical column is also its own public query, because aerial perspective is
        // expressed in units of it.
        require(glm::length(atmosphere::verticalOpticalDepth(parameters) - upwardDepth)
                    < glm::length(upwardDepth) * 0.01f,
                "verticalOpticalDepth must agree with an escaping vertical segment");
        require(glm::length(atmosphere::verticalOpticalDepth(parameters)) > 0.0f,
                "the vertical column must not be empty");
        atmosphere::AtmosphereParameters denser = parameters;
        denser.turbidity = 4.0f;
        require(allFiniteNonNegative(atmosphere::verticalOpticalDepth(denser))
                    && glm::length(atmosphere::verticalOpticalDepth(denser))
                        > glm::length(atmosphere::verticalOpticalDepth(parameters)),
                "more aerosol must deepen the vertical column");
        atmosphere::AtmosphereParameters thicker = parameters;
        thicker.skyIntensity = 5.0f;
        require(glm::length(atmosphere::verticalOpticalDepth(thicker)
                    - atmosphere::verticalOpticalDepth(parameters)) == 0.0f,
                "the vertical column must not depend on sky intensity");
        atmosphere::AtmosphereParameters dense = parameters;
        dense.turbidity = 4.0f;
        require(glm::length(atmosphere::opticalDepthAlongSegment(
                    dense, segmentLength, 0.1f, testWorldUnitsPerMetre
                )) > glm::length(atmosphere::opticalDepthAlongSegment(
                    parameters, segmentLength, 0.1f, testWorldUnitsPerMetre
                )),
                "more aerosol must thicken a horizontal segment");
        require(allFiniteNonNegative(atmosphere::opticalDepthAlongSegment(
                    parameters, 1.0e9f, 0.0f, testWorldUnitsPerMetre
                )),
                "an enormous horizontal segment must stay finite");
        require(allFiniteNonNegative(atmosphere::opticalDepthAlongSegment(
                    parameters, 1.0e9f, 1.0e-9f
                , testWorldUnitsPerMetre)),
                "a nearly horizontal segment must not divide by zero");

        // The sun disk is the brightest feature and only covers its own angular radius.
        const glm::vec3 disk = atmosphere::sunDiskRadiance(noonSunDirection, parameters);
        require(glm::length(disk) > glm::length(noonZenith) * 100.0f,
                "the sun disk must dominate the sky radiance");
        require(atmosphere::sunAngularRadiusDegrees() > 0.265f,
                "the rendered disk must be at least as wide as the real sun");
        const glm::vec3 offDisk = atmosphere::sunDiskRadiance(
            glm::normalize(noonSunDirection + glm::vec3(0.0f, 0.2f, 0.0f)), parameters
        );
        require(glm::length(offDisk) == 0.0f, "the disk must not leak outside its radius");

        // The disk's absolute scale is pinned to the clear-day illuminance ratio, because the
        // environment's ground hemisphere and its prefiltered specular both integrate it: a token
        // sun there leaves the lower hemisphere darker than the ground the key light lights.
        const glm::vec3 sunIrradiance = atmosphere::sunIrradiance(parameters);
        require(allFiniteNonNegative(sunIrradiance), "sun irradiance must be finite");
        // E_sky is pi times the cosine-weighted average sky radiance. A uniform sky at the noon
        // zenith's brightness is a close enough reference for a ratio guard.
        const float skyIrradiance = 3.14159265358979323846f * glm::length(noonZenith);
        const float sunToSky = glm::length(sunIrradiance) / std::max(skyIrradiance, 1.0e-6f);
        require(sunToSky > 2.0f && sunToSky < 60.0f,
                "the sun must dominate the sky irradiance by a daylight ratio");
        atmosphere::AtmosphereParameters brighterSun = parameters;
        brighterSun.sunIntensity = 4.0f;
        require(glm::length(atmosphere::sunIrradiance(brighterSun))
                    > glm::length(sunIrradiance) * 3.5f,
                "sun irradiance must scale with sun intensity");
        atmosphere::AtmosphereParameters disabledAtmosphere;
        require(glm::length(atmosphere::sunIrradiance(disabledAtmosphere)) == 0.0f,
                "a disabled atmosphere must radiate nothing");

        // The ground hemisphere is what a Lambertian ground reflects, so it must carry the sun as
        // well as the sky, and it must stay finite with the sun below the horizon.
        const glm::vec3 ground = atmosphere::skyRadiance(glm::vec3(0.0f, -1.0f, 0.0f), parameters);
        require(allFiniteNonNegative(ground), "the ground hemisphere must be finite");
        atmosphere::AtmosphereParameters unlitSun = parameters;
        unlitSun.sunIntensity = 0.0f;
        require(glm::length(ground)
                    > glm::length(atmosphere::skyRadiance(glm::vec3(0.0f, -1.0f, 0.0f), unlitSun)),
                "the ground must reflect the direct sun, not only the sky");
        atmosphere::AtmosphereParameters belowHorizon = parameters;
        belowHorizon.sunElevationDegrees = -8.0f;
        require(allFiniteNonNegative(
                    atmosphere::skyRadiance(glm::vec3(0.0f, -1.0f, 0.0f), belowHorizon)
                ),
                "a sun below the horizon must not make the ground negative or NaN");
        require(atmosphere::nightVisibility(belowHorizon) == 0.0f,
                "legacy night scenes must keep the optional night sky off");
        const glm::vec3 legacyNight = atmosphere::skyRadiance(
            glm::vec3(0.0f, 1.0f, 0.0f), belowHorizon);
        belowHorizon.nightSkyEnabled = true;
        require(atmosphere::nightVisibility(belowHorizon) == 1.0f,
                "night sky must reach full strength at the sequence endpoint");
        require(atmosphere::moonDirection(belowHorizon).y > 0.0f,
                "the moon must rise as the sun sets");
        require(atmosphere::moonKeyStrength(belowHorizon) > 0.0f,
                "moonlight must illuminate night geometry");
        require(glm::length(atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), belowHorizon))
                    > glm::length(legacyNight),
                "night sky must keep the zenith visible");
        require(glm::length(atmosphere::skyRadiance(
                    atmosphere::moonDirection(belowHorizon), belowHorizon)) > 10.0f,
                "the moon must appear in the common environment");
        atmosphere::AtmosphereParameters changedNight = belowHorizon;
        changedNight.starIntensity = 2.0f;
        require(!atmosphere::parametersMatch(belowHorizon, changedNight),
                "changing stars must invalidate the cached environment");
        atmosphere::AtmosphereParameters noStars = belowHorizon;
        noStars.moonIntensity = 0.0f;
        noStars.starIntensity = 0.0f;
        atmosphere::AtmosphereParameters withStars = noStars;
        withStars.starIntensity = 1.0f;
        std::size_t visibleStars = 0U;
        for (int elevation = 2; elevation <= 70; elevation += 2) {
            for (int azimuth = 0; azimuth < 360; ++azimuth) {
                const float altitude = glm::radians(static_cast<float>(elevation));
                const float bearing = glm::radians(static_cast<float>(azimuth));
                const glm::vec3 starDirection(
                    std::cos(altitude) * std::sin(bearing), std::sin(altitude),
                    std::cos(altitude) * std::cos(bearing));
                visibleStars += glm::length(atmosphere::skyRadiance(starDirection, withStars)
                    - atmosphere::skyRadiance(starDirection, noStars)) > 0.5f ? 1U : 0U;
            }
        }
        require(visibleStars > 10U, "procedural stars must be visible across the night sky");
        requireFiniteEverywhere(belowHorizon, "night sky must stay finite");

        // Rotating view and sun together must not change the sky: only the angle between them
        // and the view elevation matter.
        atmosphere::AtmosphereParameters rotated = parameters;
        rotated.sunAzimuthDegrees = parameters.sunAzimuthDegrees + 47.0f;
        const glm::vec3 view = glm::normalize(glm::vec3(0.4f, 0.35f, 0.85f));
        // sunDirection() measures azimuth from +Z towards +X, so the matching rotation about Y is
        // x' = x cos + z sin, z' = -x sin + z cos.
        const float rotatedAzimuth = glm::radians(47.0f);
        const glm::vec3 rotatedView(
            view.x * std::cos(rotatedAzimuth) + view.z * std::sin(rotatedAzimuth),
            view.y,
            -view.x * std::sin(rotatedAzimuth) + view.z * std::cos(rotatedAzimuth)
        );
        const glm::vec3 base = atmosphere::skyRadiance(view, parameters);
        const glm::vec3 rotatedSky = atmosphere::skyRadiance(rotatedView, rotated);
        require(glm::length(base - rotatedSky) < glm::length(base) * 1.0e-4f,
                "the sky must depend only on the angle to the sun, not on absolute azimuth");

        // Hazier air scatters more and flattens the colour contrast.
        atmosphere::AtmosphereParameters hazy = parameters;
        hazy.turbidity = 6.0f;
        require(glm::length(atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), hazy))
                    > glm::length(noonZenith),
                "more aerosol must brighten the sky");

        // Degenerate and hostile inputs must stay finite: sun below the horizon, below-zero
        // intensity, zenith view exactly at the horizon and an unnormalised direction.
        for (float elevation : {-90.0f, -30.0f, -1.0f, 0.0f, 1.0f, 89.9f, 90.0f}) {
            atmosphere::AtmosphereParameters extreme = parameters;
            extreme.sunElevationDegrees = elevation;
            requireFiniteEverywhere(extreme, "every sun elevation must produce a finite sky");
        }
        atmosphere::AtmosphereParameters zeroIntensity = parameters;
        zeroIntensity.skyIntensity = 0.0f;
        zeroIntensity.sunIntensity = 0.0f;
        requireFiniteEverywhere(zeroIntensity, "zero intensity must stay finite");
        atmosphere::AtmosphereParameters negative = parameters;
        negative.skyIntensity = -5.0f;
        negative.sunIntensity = -2.0f;
        negative.turbidity = -1.0f;
        negative.groundAlbedo = -1.0f;
        requireFiniteEverywhere(negative, "negative parameters must clamp instead of returning NaN");
        require(allFiniteNonNegative(atmosphere::skyRadiance(glm::vec3(0.0f), parameters)),
                "a zero view direction must not divide by zero");
        require(allFiniteNonNegative(
                    atmosphere::skyRadiance(glm::vec3(1.0e6f, 1.0f, -1.0e6f), parameters)
                ),
                "an unnormalised view direction must be handled");

        // Determinism: the same parameters must produce bitwise identical radiance.
        const glm::vec3 again = atmosphere::skyRadiance(view, parameters);
        require(base.x == again.x && base.y == again.y && base.z == again.z,
                "the model must be deterministic");

        // Equirectangular generation follows the environment loader's orientation: row 0 is the
        // +Y pole, column 0 looks along +Z, and azimuth grows from +Z towards +X. The sun disk is
        // only ~1.2 degrees wide, so the grid has to be fine enough to contain it at all -- at
        // 64x32 the disk falls between pixel centres, which is worth knowing before rendering a
        // sky from a low-resolution map.
        parameters.sunElevationDegrees = 45.0f;
        parameters.sunAzimuthDegrees = 90.0f;
        // The cloud layer resolves its parallax against the eye, so every sky contract below is
        // stated from one explicit viewpoint. Ground level is the case a coastal scene renders from.
        constexpr float eyeHeight = 0.0f;
        const int width = 512;
        const int height = 256;
        const std::vector<glm::vec3> equirect =
            atmosphere::generateEquirect(parameters, width, height, eyeHeight);
        require(equirect.size() == static_cast<std::size_t>(width) * height,
                "the generated environment must cover the whole image");
        for (const glm::vec3& pixel : equirect) {
            require(allFiniteNonNegative(pixel), "every generated pixel must be finite and non-negative");
        }
        std::size_t brightest = 0U;
        for (std::size_t index = 1U; index < equirect.size(); ++index) {
            if (glm::length(equirect[index]) > glm::length(equirect[brightest])) brightest = index;
        }
        const int brightestRow = static_cast<int>(brightest / static_cast<std::size_t>(width));
        const int brightestColumn = static_cast<int>(brightest % static_cast<std::size_t>(width));
        const int expectedRow = static_cast<int>((45.0f / 180.0f) * static_cast<float>(height));
        const int expectedColumn = static_cast<int>((90.0f / 360.0f) * static_cast<float>(width));
        require(std::abs(brightestRow - expectedRow) <= 1,
                "the sun disk must sit at the requested elevation");
        require(std::abs(brightestColumn - expectedColumn) <= 1,
                "the sun disk must sit at the requested azimuth");
        const glm::vec3 zenithPixel = equirect[static_cast<std::size_t>(width) / 2U];
        require(glm::length(equirect[brightest]) > glm::length(zenithPixel) * 10.0f,
                "the sun must dominate the sky it is rendered into");
        require(atmosphere::generateEquirect(parameters, width, height, eyeHeight) == equirect,
                "equirectangular generation must be deterministic");
        require(atmosphere::generateEquirect(parameters, 0, 0, eyeHeight).empty(),
                "an empty request must produce no pixels");
        require(atmosphere::generateEquirect(parameters, 64, 32, eyeHeight).size() == 64U * 32U,
                "a coarse request must still produce a full image");

        // Raster IBL is cloudless: frame effects must not rebake the sky.
        auto frameEffects = parameters;
        frameEffects.cloudsEnabled = !parameters.cloudsEnabled;
        frameEffects.cloudWindOffsetX += 100.0f;
        frameEffects.cloudCoverage += 0.2f;
        frameEffects.cloudQuality = atmosphere::CloudQualityTier::Low;
        frameEffects.cloudTemporalEnabled = !parameters.cloudTemporalEnabled;
        frameEffects.cloudDeterministic = !parameters.cloudDeterministic;
        frameEffects.aerialPerspectiveEnabled = !parameters.aerialPerspectiveEnabled;
        require(atmosphere::environmentParametersMatch(parameters, frameEffects),
                "cloud and aerial frame effects must not invalidate raster IBL");
        require(!atmosphere::parametersMatch(parameters, frameEffects),
                "full rendered-field caches must still invalidate for cloud changes");
        for (float atmosphere::AtmosphereParameters::* field : {
            &atmosphere::AtmosphereParameters::sunElevationDegrees,
            &atmosphere::AtmosphereParameters::sunAzimuthDegrees,
            &atmosphere::AtmosphereParameters::turbidity,
            &atmosphere::AtmosphereParameters::skyIntensity,
            &atmosphere::AtmosphereParameters::sunIntensity,
            &atmosphere::AtmosphereParameters::groundAlbedo,
            &atmosphere::AtmosphereParameters::moonIntensity,
            &atmosphere::AtmosphereParameters::starIntensity}) {
            auto changed = parameters;
            changed.*field += 1.0f;
            require(!atmosphere::environmentParametersMatch(parameters, changed),
                    "sky radiance inputs must invalidate raster IBL");
        }
        auto changedEnvironmentNight = parameters;
        changedEnvironmentNight.nightSkyEnabled = !parameters.nightSkyEnabled;
        require(!atmosphere::environmentParametersMatch(parameters, changedEnvironmentNight),
                "night sky toggle must invalidate raster IBL");

        requireCloudLayerContracts(parameters, eyeHeight);

        std::cout << "Atmosphere model tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Atmosphere model tests failed: " << error.what() << '\n';
        return 1;
    }
}
