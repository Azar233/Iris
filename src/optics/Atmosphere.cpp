#include "optics/Atmosphere.h"

// Included before the standard library headers on purpose: it defines GLSL-compatible shims for
// `clamp` / `sqrt` / `floor` / `smoothstep` and releases them again at its end, so this order is
// what keeps those names confined to the header's own body.
#include "optics/CloudParams.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace atmosphere {
namespace {

constexpr float pi = 3.14159265358979323846f;
// Earth-like scale heights in metres.
constexpr float rayleighScaleHeight = 8000.0f;
constexpr float mieScaleHeight = 1200.0f;
// Sea-level scattering coefficients (1/m). Rayleigh is what makes the sky blue; the Mie
// term is grey and only shaped by its phase function.
const glm::vec3 rayleighCoefficient(5.8e-6f, 13.5e-6f, 33.1e-6f);
constexpr float mieCoefficient = 21.0e-6f;
// Henyey-Greenstein anisotropy for a mixed rural aerosol. The literature spans roughly
// 0.5-0.8; 0.76-0.8 concentrates the aureole so strongly that the grey forward-scattered
// term washes the zenith out, which is not what a clear sky looks like.
constexpr float mieAnisotropy = 0.5f;
// The sun disk's radiance relative to the sky. The absolute scale is arbitrary -- the RGB values
// here are not photometric -- so it is pinned to the one ratio that is observable in the render:
// the clear-day illuminance ratio `E_sun / E_sky` of roughly 10. The disk subtends
// `2*pi*(1 - cos(0.6 deg))`, about 3.4e-4 sr, so a radiance of 4.6e3 against the sky's ~0.1
// radiance lands in that range. That matters beyond looks: the environment's ground hemisphere
// and its prefiltered specular both integrate the disk, so a token-bright sun there would leave
// reflections sunless and the lower hemisphere darker than the ground the key light actually
// lights.
constexpr float sunDiskRadianceScale = 4.6e3f;
constexpr float sunAngularRadius = 0.6f;
constexpr float moonAngularRadius = 1.3f; // wide enough to survive the environment map
float saturate(float value);

std::uint32_t starHash(std::uint32_t x) {
    x ^= x >> 16U;
    x *= 0x7feb352dU;
    x ^= x >> 15U;
    x *= 0x846ca68bU;
    return x ^ (x >> 16U);
}

} // namespace

// The cloud density field lives in `optics/CloudFieldCpp.h`, which is written to be included verbatim
// by both this translation unit and `shaders/cloud_layer.frag`. The public entry points below are
// thin wrappers over it, so the analytic layer here and the CPU reference raymarcher in
// `CloudReference.cpp` and the GPU march all evaluate one implementation rather than three
// transcriptions that can drift apart.
float worley2x2(float x, float y, int period) {
    return myrenderer_cloud_worley(x, y, period);
}

float cloudShape(float tileX, float tileY, int period) {
    return myrenderer_cloud_base_shape(tileX, tileY, period);
}

float cloudDetailShape(float tileX, float tileY, int period) {
    return myrenderer_cloud_detail_shape(tileX, tileY, period);
}

float cloudWeather(float tileX, float tileY, int channel) {
    return myrenderer_cloud_weather(tileX, tileY, channel);
}

namespace {

glm::vec3 nightRadiance(const glm::vec3& direction, const AtmosphereParameters& parameters) {
    const float visibility = nightVisibility(parameters);
    if (!parameters.nightSkyEnabled || direction.y <= 0.0f) return glm::vec3(0.0f);
    const float fillT = saturate((10.0f - parameters.sunElevationDegrees) / 18.0f);
    const float twilightFill = fillT * fillT * (3.0f - 2.0f * fillT);
    if (twilightFill <= 0.0f) return glm::vec3(0.0f);
    const glm::vec3 moon = moonDirection(parameters);
    const float moonGlow = std::pow(std::max(glm::dot(direction, moon), 0.0f), 12.0f);
    glm::vec3 result = glm::vec3(0.040f, 0.060f, 0.110f) * twilightFill
        + glm::vec3(0.035f, 0.045f, 0.070f) * moonGlow
            * std::max(parameters.moonIntensity, 0.0f) * visibility;

    // Stable spherical cells yield small stars with no frame-dependent random state.
    // Centers stay inside their cells, so one lookup per sky sample is sufficient.
    const float phi = std::atan2(direction.x, direction.z) + pi;
    const float theta = std::acos(std::clamp(direction.y, 0.0f, 1.0f));
    const float u = phi * (64.0f / (2.0f * pi));
    const float v = theta * (48.0f / pi);
    const int cellX = static_cast<int>(u);
    const int cellY = static_cast<int>(v);
    const std::uint32_t hash = starHash(static_cast<std::uint32_t>(cellX)
        + 131U * static_cast<std::uint32_t>(cellY));
    if ((hash & 3U) == 0U) {
        const float centerU = static_cast<float>(cellX) + 0.35f
            + 0.3f * static_cast<float>((hash >> 8U) & 255U) / 255.0f;
        const float centerV = static_cast<float>(cellY) + 0.35f
            + 0.3f * static_cast<float>((hash >> 16U) & 255U) / 255.0f;
        const float angularX = (u - centerU) * (2.0f * pi / 64.0f) * std::sin(theta);
        const float angularY = (v - centerV) * (pi / 48.0f);
        const float distanceDegrees = glm::degrees(std::sqrt(angularX * angularX + angularY * angularY));
        const float star = saturate((0.45f - distanceDegrees) / 0.25f);
        const float brightness = 1.5f + 1.5f * static_cast<float>((hash >> 24U) & 255U) / 255.0f;
        result += glm::vec3(0.75f, 0.84f, 1.0f) * star * brightness
            * std::max(parameters.starIntensity, 0.0f) * visibility;
    }
    const float cosAngle = glm::dot(direction, moon);
    const float cosRadius = std::cos(glm::radians(moonAngularRadius));
    if (moon.y > 0.0f && cosAngle > cosRadius) {
        const float edge = saturate((cosAngle - cosRadius)
            / std::max(1.0f - cosRadius, 1.0e-5f) * 8.0f);
        result += glm::vec3(22.0f, 27.0f, 36.0f) * edge
            * std::max(parameters.moonIntensity, 0.0f) * visibility;
    }
    return result;
}

// Normalises defensively: a degenerate direction (for example a zero vector from an
// uninitialised uniform) must never turn into NaN radiance downstream.
bool normaliseDirection(const glm::vec3& value, glm::vec3& result) {
    const float lengthSquared = glm::dot(value, value);
    if (!(lengthSquared > 1.0e-12f)) return false;
    result = value / std::sqrt(lengthSquared);
    return true;
}

float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float rayleighPhase(float cosTheta) {
    return (3.0f / (16.0f * pi)) * (1.0f + cosTheta * cosTheta);
}

float miePhase(float cosTheta) {
    const float g = mieAnisotropy;
    const float g2 = g * g;
    const float denominator = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (4.0f * pi * std::max(denominator * std::sqrt(std::max(denominator, 1.0e-6f)), 1.0e-6f));
}

// Kasten-Young relative air mass for a zenith angle in degrees. Clamped so a sun or a view
// ray at or below the horizon still yields a finite, monotone value.
float relativeAirMass(float zenithDegrees) {
    const float clamped = std::clamp(zenithDegrees, 0.0f, 96.0f);
    const float cosine = std::cos(glm::radians(clamped));
    const float horizonTerm = std::pow(std::max(96.07995f - clamped, 0.01f), -1.6364f);
    return 1.0f / std::max(cosine + 0.50572f * horizonTerm, 1.0e-4f);
}

glm::vec3 rayleighOpticalDepth(float airMass) {
    return rayleighCoefficient * (rayleighScaleHeight * airMass);
}

float mieOpticalDepth(float airMass, const AtmosphereParameters& parameters) {
    return mieCoefficient * std::max(parameters.turbidity, 0.0f) * (mieScaleHeight * airMass);
}

// Single-scattering radiance for a view ray in the upper hemisphere. Shared by the public sky
// query and the ground-irradiance proxy so the two can never drift apart.
glm::vec3 scatteringRadiance(const glm::vec3& direction, const AtmosphereParameters& parameters) {
    const glm::vec3 sun = sunDirection(parameters);
    const float viewZenithDegrees = glm::degrees(std::acos(std::clamp(direction.y, -1.0f, 1.0f)));
    const float sunZenithDegrees = glm::degrees(std::acos(std::clamp(sun.y, -1.0f, 1.0f)));
    const float viewAirMass = relativeAirMass(viewZenithDegrees);
    const float sunAirMass = relativeAirMass(sunZenithDegrees);

    const glm::vec3 viewOpticalDepth = rayleighOpticalDepth(viewAirMass)
        + glm::vec3(mieOpticalDepth(viewAirMass, parameters));
    const glm::vec3 totalOpticalDepth = viewOpticalDepth
        + rayleighOpticalDepth(sunAirMass)
        + glm::vec3(mieOpticalDepth(sunAirMass, parameters));
    const glm::vec3 transmittance = glm::exp(-totalOpticalDepth);

    // Closed-form integral of an exponential atmosphere along the view ray: each component
    // contributes `beta * H * m * (1 - exp(-tau)) / tau`.
    const glm::vec3 rayleighIntegral = rayleighCoefficient * (rayleighScaleHeight * viewAirMass)
        * (glm::vec3(1.0f) - transmittance) / glm::max(totalOpticalDepth, glm::vec3(1.0e-12f));
    const glm::vec3 mieIntegral = glm::vec3(
        mieCoefficient * std::max(parameters.turbidity, 0.0f) * (mieScaleHeight * viewAirMass)
    ) * (glm::vec3(1.0f) - transmittance) / glm::max(totalOpticalDepth, glm::vec3(1.0e-12f));

    const float cosTheta = glm::dot(direction, sun);
    const glm::vec3 scattering = rayleighIntegral * rayleighPhase(cosTheta)
        + mieIntegral * miePhase(cosTheta);
    return scattering * sunTransmittance(parameters)
        * std::max(parameters.skyIntensity, 0.0f) * std::max(parameters.sunIntensity, 0.0f);
}

// Cosine-weighted average sky radiance over the upper hemisphere, which is what a Lambertian
// ground reflects back as `albedo * irradiance / pi`. Sampling the zenith alone (the first cut)
// left the lower hemisphere black whenever a low sun concentrates the sky into a horizon glow,
// which shows up as a black band under the horizon in any frame that sees past the ground
// geometry. Nine fixed directions -- the pole plus rings at 45 and 75 degrees -- give a smooth
// deterministic gradient for far less work than the per-texel exponential sweep around it.
glm::vec3 skyIrradianceOverPi(const AtmosphereParameters& parameters) {
    glm::vec3 accumulated(0.0f);
    float weightSum = 0.0f;
    for (int ring = 0; ring < 3; ++ring) {
        const float polarDegrees = ring == 0 ? 0.0f : (ring == 1 ? 45.0f : 75.0f);
        const int samples = ring == 0 ? 1 : 4;
        const float polar = glm::radians(polarDegrees);
        const float sampleWeight = std::cos(polar);
        for (int sample = 0; sample < samples; ++sample) {
            const float azimuth = 2.0f * pi * (static_cast<float>(sample) + 0.5f)
                / static_cast<float>(samples);
            const glm::vec3 direction(
                std::sin(polar) * std::sin(azimuth),
                std::cos(polar),
                std::sin(polar) * std::cos(azimuth)
            );
            accumulated += scatteringRadiance(direction, parameters) * sampleWeight;
            weightSum += sampleWeight;
        }
    }
    return accumulated / std::max(weightSum, 1.0e-6f);
}

} // namespace

glm::vec3 sunDirection(const AtmosphereParameters& parameters) {
    const float elevation = glm::radians(parameters.sunElevationDegrees);
    const float azimuth = glm::radians(parameters.sunAzimuthDegrees);
    const float horizontal = std::cos(elevation);
    return glm::vec3(
        horizontal * std::sin(azimuth),
        std::sin(elevation),
        horizontal * std::cos(azimuth)
    );
}

glm::vec3 moonDirection(const AtmosphereParameters& parameters) {
    // A visual day/night orbit with an offset that puts the moon in the coastal camera's
    // evening sky. A calendar-based orbit can replace this mapping later.
    const float elevation = glm::radians(-parameters.sunElevationDegrees * 0.6f);
    const float azimuth = glm::radians(parameters.sunAzimuthDegrees - 90.0f);
    const float horizontal = std::cos(elevation);
    return glm::vec3(horizontal * std::sin(azimuth), std::sin(elevation),
        horizontal * std::cos(azimuth));
}

float nightVisibility(const AtmosphereParameters& parameters) {
    if (!parameters.enabled || !parameters.nightSkyEnabled) return 0.0f;
    const float t = saturate((-parameters.sunElevationDegrees - 2.0f) / 6.0f);
    return t * t * (3.0f - 2.0f * t);
}

float moonKeyStrength(const AtmosphereParameters& parameters) {
    return nightVisibility(parameters) * std::max(parameters.moonIntensity, 0.0f) * 0.55f;
}

bool environmentParametersMatch(const AtmosphereParameters& a, const AtmosphereParameters& b) {
    if (a.enabled != b.enabled) return false;
    if (a.nightSkyEnabled != b.nightSkyEnabled) return false;
    // The sun tolerances are angular degrees of movement; the rest are absolute parameter units.
    // Both are set at the point where the change stops being visible as noise in a rendered frame.
    constexpr float sunTolerance = 0.35f;
    constexpr float parameterTolerance = 0.01f;
    return std::abs(a.sunElevationDegrees - b.sunElevationDegrees) < sunTolerance
        && std::abs(a.sunAzimuthDegrees - b.sunAzimuthDegrees) < sunTolerance
        && std::abs(a.turbidity - b.turbidity) < parameterTolerance
        && std::abs(a.skyIntensity - b.skyIntensity) < parameterTolerance
        && std::abs(a.sunIntensity - b.sunIntensity) < parameterTolerance
        && std::abs(a.groundAlbedo - b.groundAlbedo) < parameterTolerance
        && std::abs(a.moonIntensity - b.moonIntensity) < parameterTolerance
        && std::abs(a.starIntensity - b.starIntensity) < parameterTolerance;
}

bool parametersMatch(const AtmosphereParameters& a, const AtmosphereParameters& b) {
    constexpr float parameterTolerance = 0.01f;
    return environmentParametersMatch(a, b)
        // Aerial perspective does not change the environment cubemap, but it does change what a
        // cached sky is used *for*, so a consumer caching derived data must see it as a change.
        && a.aerialPerspectiveEnabled == b.aerialPerspectiveEnabled
        && std::abs(a.aerialPerspectiveStrength - b.aerialPerspectiveStrength) < parameterTolerance
        && std::abs(a.aerialPerspectiveScaleHeight - b.aerialPerspectiveScaleHeight)
            < parameterTolerance
        // The cloud layer is part of the environment it is composited into, so every one of its
        // parameters belongs in the full rendered-field cache key. Wind offsets are world-unit
        // translations; the raster environment uses environmentParametersMatch instead.
        && a.cloudsEnabled == b.cloudsEnabled
        && std::abs(a.cloudBaseHeight - b.cloudBaseHeight) < parameterTolerance
        && std::abs(a.cloudTopHeight - b.cloudTopHeight) < parameterTolerance
        && std::abs(a.cloudCoverage - b.cloudCoverage) < parameterTolerance
        && std::abs(a.cloudDensity - b.cloudDensity) < parameterTolerance
        && std::abs(a.cloudWindOffsetX - b.cloudWindOffsetX) < parameterTolerance
        && std::abs(a.cloudWindOffsetZ - b.cloudWindOffsetZ) < parameterTolerance
        && std::abs(a.cloudFeatureScale - b.cloudFeatureScale) < parameterTolerance
        && std::abs(a.cloudNoisePeriod - b.cloudNoisePeriod) < parameterTolerance
        // The weather map's controls belong in the key for the same reason the wind offsets do: they
        // move density across the sky, so a consumer caching anything derived from the layer has to
        // see them change.
        && std::abs(a.cloudWeatherScale - b.cloudWeatherScale) < parameterTolerance
        && std::abs(a.cloudCoverageVariation - b.cloudCoverageVariation) < parameterTolerance
        && std::abs(a.cloudType - b.cloudType) < parameterTolerance
        && std::abs(a.cloudTypeVariation - b.cloudTypeVariation) < parameterTolerance
        && std::abs(a.cloudHeightVariation - b.cloudHeightVariation) < parameterTolerance
        && std::abs(a.cloudDetailStrength - b.cloudDetailStrength) < parameterTolerance
        && std::abs(a.cloudDetailEdge - b.cloudDetailEdge) < parameterTolerance
        && a.cloudHeightLighting == b.cloudHeightLighting
        && std::abs(a.cloudShapeBlend - b.cloudShapeBlend) < parameterTolerance
        // The tier changes the integral's step count, so it moves pixels even though no field
        // parameter changed. A cache that ignored it would hand a High-tier consumer a Low-tier bake.
        && a.cloudQuality == b.cloudQuality
        && a.cloudHalfResolution == b.cloudHalfResolution
        && a.cloudTemporalEnabled == b.cloudTemporalEnabled
        && a.cloudShadowsEnabled == b.cloudShadowsEnabled
        && a.cloudGodRaysEnabled == b.cloudGodRaysEnabled
        && a.cloudDeterministic == b.cloudDeterministic
        && a.cloudOfflineNoise == b.cloudOfflineNoise
        && std::abs(a.cloudGodRaysStrength - b.cloudGodRaysStrength) < parameterTolerance
        && std::abs(a.cloudHorizonFadeDegrees - b.cloudHorizonFadeDegrees) < parameterTolerance
        // The two ambient values change what every cloud texel is lit by, so they are part of the
        // environment too.
        && std::abs(a.cloudAmbientElevationDegrees - b.cloudAmbientElevationDegrees)
            < parameterTolerance
        && std::abs(a.cloudAmbientScale - b.cloudAmbientScale) < parameterTolerance
        && std::abs(a.cloudVolumetricAmbientScale - b.cloudVolumetricAmbientScale)
            < parameterTolerance
        && std::abs(a.cloudVolumetricSunScale - b.cloudVolumetricSunScale) < parameterTolerance;
}

glm::vec3 sunTransmittance(const AtmosphereParameters& parameters) {
    const glm::vec3 direction = sunDirection(parameters);
    // A sun below the horizon keeps a finite optical depth instead of exploding, so the
    // directional light fades out smoothly rather than switching off with a discontinuity.
    const float zenithDegrees = glm::degrees(std::acos(std::clamp(direction.y, -1.0f, 1.0f)));
    const float sunAirMass = relativeAirMass(zenithDegrees);
    return glm::exp(-(rayleighOpticalDepth(sunAirMass) + glm::vec3(mieOpticalDepth(sunAirMass, parameters))));
}

glm::vec3 opticalDepthAlongSegment(
    const AtmosphereParameters& parameters,
    float segmentLength,
    float directionY,
    float worldUnitsPerMetre
) {
    // The scene's world units are converted here rather than assumed, so a caller that models a
    // metre as one unit and a caller that models a kilometre as one unit reach the same optical
    // depth for the same physical distance.
    const float unitScale = worldUnitsPerMetre > 1.0e-9f ? 1.0f / worldUnitsPerMetre : 1.0f;
    const float length = std::max(segmentLength, 0.0f) * unitScale;
    if (!(length > 0.0f) || !std::isfinite(length)) return glm::vec3(0.0f);

    // A horizontal segment is the limit of the expression below, so the vertical component is
    // clamped away from zero and the exponential is written as `(1 - exp(-x)) / x * len` with
    // `x = len * y / scaleHeight`: at `y -> 0` that tends to `len / scaleHeight` instead of
    // dividing by zero.
    constexpr float minimumVerticalComponent = 1.0e-4f;
    const float vertical = std::max(std::abs(directionY), minimumVerticalComponent);
    // The air mass is the one the sky already uses, evaluated for the ray's own zenith angle, so a
    // ray that points up accumulates exactly the column `sunTransmittance` describes.
    const float zenithDegrees = glm::degrees(std::acos(std::clamp(vertical, 0.0f, 1.0f)));
    const float airMass = relativeAirMass(zenithDegrees);

    const float heightDelta = length * directionY;
    const float scaledHeight = heightDelta / rayleighScaleHeight;
    // Both constituents ride the same relative density profile: `beta * H * airMass * (1 - e^-x)`
    // with `x = heightDelta / H`, which is `beta * H * airMass * len / H` in the horizontal limit
    // and `beta * H * airMass` once the segment reaches the top of the atmosphere. That is the
    // same column `rayleighOpticalDepth` / `mieOpticalDepth` describe, so a ray that escapes
    // saturates on exactly the value `sunTransmittance` is built from. Using one scale height for
    // the path integral while each constituent keeps its own magnitude is the approximation this
    // shares with the rest of the model: it is exact at both limits and never overshoots between
    // them.
    const float columnFactor = std::abs(scaledHeight) < 1.0e-4f
        ? length / rayleighScaleHeight
        : (1.0f - std::exp(-scaledHeight)) * (length / heightDelta);

    const glm::vec3 rayleigh = rayleighCoefficient * (rayleighScaleHeight * airMass * columnFactor);
    const float mie = mieOpticalDepth(airMass, parameters) * columnFactor;
    const glm::vec3 result = rayleigh + glm::vec3(mie);
    // An upward ray through a tall column can overflow the exponential; clamping keeps a caller's
    // `exp(-depth)` at zero rather than at a NaN.
    return glm::max(result, glm::vec3(0.0f));
}

glm::vec3 verticalOpticalDepth(const AtmosphereParameters& parameters) {
    // A sun exactly at the zenith is the one configuration where `sunTransmittance` already *is* the
    // vertical column, so the column is defined by evaluating it there rather than by a second
    // implementation of the same sum.
    AtmosphereParameters overhead = parameters;
    overhead.enabled = true;
    overhead.sunElevationDegrees = 90.0f;
    overhead.sunAzimuthDegrees = 0.0f;
    const glm::vec3 transmittance = sunTransmittance(overhead);
    return glm::max(-glm::log(glm::max(transmittance, glm::vec3(1.0e-30f))), glm::vec3(0.0f));
}

glm::vec3 skyLightColor(const AtmosphereParameters& parameters) {
    if (!parameters.enabled) return glm::vec3(1.0f);
    const glm::vec3 transmittance = sunTransmittance(parameters);
    const float brightest = std::max(transmittance.r, std::max(transmittance.g, transmittance.b));
    // An atmosphere that has extinguished the sun completely keeps a neutral light: the key
    // light's energy is already carried by `sunTransmittance`'s luminance, so dividing by a
    // vanishing channel would only amplify float noise into a saturated colour.
    if (!std::isfinite(brightest) || brightest <= 1.0e-6f) return glm::vec3(1.0f);
    return glm::min(transmittance / brightest, glm::vec3(1.0f));
}

glm::vec3 sunDiskRadiance(const glm::vec3& viewDirection, const AtmosphereParameters& parameters) {
    if (!parameters.enabled) return glm::vec3(0.0f);
    glm::vec3 direction;
    if (!normaliseDirection(viewDirection, direction)) return glm::vec3(0.0f);
    const glm::vec3 sun = sunDirection(parameters);
    const float cosAngle = glm::dot(direction, sun);
    const float cosRadius = std::cos(glm::radians(sunAngularRadius));
    if (cosAngle < cosRadius) return glm::vec3(0.0f);
    // Soft edge over the last tenth of the disk so the boundary is not a hard pixel step.
    const float edge = saturate((cosAngle - cosRadius) / std::max(1.0f - cosRadius, 1.0e-5f) * 10.0f);
    return sunTransmittance(parameters) * sunDiskRadianceScale
        * std::max(parameters.sunIntensity, 0.0f) * edge;
}

glm::vec3 skyRadiance(const glm::vec3& viewDirection, const AtmosphereParameters& parameters) {
    if (!parameters.enabled) return glm::vec3(0.0f);
    glm::vec3 direction;
    if (!normaliseDirection(viewDirection, direction)) return glm::vec3(0.0f);

    if (direction.y < 0.0f) {
        // Ground hemisphere: what a Lambertian ground of `groundAlbedo` reflects, which is
        // `albedo/pi * (E_sky + E_sun)`. Both terms are needed -- the sky alone leaves the lower
        // hemisphere far darker than the ground the key light lights, which reads as a dark band
        // under the horizon in any frame that sees past the ground geometry.
        const glm::vec3 irradianceOverPi = skyIrradianceOverPi(parameters)
            + sunIrradiance(parameters) * (std::max(sunDirection(parameters).y, 0.0f) / pi);
        const float fillT = parameters.nightSkyEnabled
            ? saturate((10.0f - parameters.sunElevationDegrees) / 18.0f) : 0.0f;
        const float twilightFill = fillT * fillT * (3.0f - 2.0f * fillT);
        return (irradianceOverPi + glm::vec3(0.040f, 0.060f, 0.110f)
            * twilightFill) * std::clamp(parameters.groundAlbedo, 0.0f, 1.0f);
    }
    return scatteringRadiance(direction, parameters) + nightRadiance(direction, parameters);
}

float cloudPhase(float cosViewSun) {
    // Double-lobed Henyey-Greenstein. Forward scattering gives the silver lining where the layer
    // sits between the camera and the sun; the weaker backward lobe keeps the anti-solar side from
    // going flat. The literature spans roughly g=0.7..0.85 forward and -0.2..-0.4 backward with a
    // near-equal blend; the values here are the middle of those ranges and are *not* a Mie solution
    // -- the analytic sky uses its own Mie phase, so the two models are not strictly consistent.
    constexpr float forwardG = 0.8f;
    constexpr float backwardG = -0.3f;
    constexpr float backwardBlend = 0.5f;
    const float clamped = std::clamp(cosViewSun, -1.0f, 1.0f);
    const auto henyeyGreenstein = [](float cosine, float g) {
        const float g2 = g * g;
        const float denominator = 1.0f + g2 - 2.0f * g * cosine;
        return (1.0f - g2) / (4.0f * pi * std::max(denominator * std::sqrt(std::max(denominator, 1.0e-6f)), 1.0e-6f));
    };
    const float forward = henyeyGreenstein(clamped, forwardG);
    const float backward = henyeyGreenstein(clamped, backwardG);
    const float mixed = forward * (1.0f - backwardBlend) + backward * backwardBlend;
    // Normalised by the isotropic value so a phase of this function averages about 1 and the
    // layer's brightness does not depend on which lobe happens to be tuned.
    return mixed * 4.0f * pi;
}

CloudLayer cloudLayer(
    const glm::vec3& viewDirection,
    const AtmosphereParameters& parameters,
    float cameraHeight
) {
    CloudLayer result;
    if (!parameters.cloudsEnabled || !parameters.enabled) return result;

    glm::vec3 direction;
    if (!normaliseDirection(viewDirection, direction)) return result;
    // The projection diverges as the view ray approaches the layer's plane. Below the horizon the
    // intersection lies behind the camera and there is nothing to draw, which is also what hides
    // the error: the layer is treated as a plane at its own height, so its true edge is never
    // reached. See the header for the scope this implies.
    if (direction.y <= 0.0f) return result;

    const float base = std::max(parameters.cloudBaseHeight, 0.0f);
    const float top = std::max(parameters.cloudTopHeight, base);
    const float slabThickness = top - base;
    // The distance at which the view ray crosses each slab boundary, and the sample point between
    // them. Sampling at the midpoint of the *crossing* rather than at a fixed height is what makes
    // the layer's vertical profile vary across the sky without a second march: a ray that crosses
    // the slab high reads a thin part of the profile, a ray that crosses it low reads a dense one.
    const float distanceToBase = std::max((base - cameraHeight) / direction.y, 0.0f);
    const float distanceToTop = std::max((top - cameraHeight) / direction.y, 0.0f);
    const float distanceToMidPlane = 0.5f * (distanceToBase + distanceToTop);
    const glm::vec3 samplePoint = glm::vec3(direction.x, direction.y, direction.z)
        * distanceToMidPlane + glm::vec3(0.0f, cameraHeight, 0.0f);

    // One density evaluation, from the same shared field the CPU reference raymarcher and the GPU
    // march evaluate. The profile is removed again because this layer applies the slab crossing as
    // an explicit thickness term below rather than through the vertical profile -- and it is removed
    // by the shared profile function itself, with the same weather-driven type blend and height
    // slide the density applied, so the division cannot leave a residue the way a re-derived profile
    // would when a later step changes the profile's shape.
    const MyRendererCloudParams layerParameters = cloud::makeCloudParams(parameters);
    const float sampledDensity = myrenderer_cloud_density(
        samplePoint.x, samplePoint.y, samplePoint.z, layerParameters);
    const float profile = myrenderer_cloud_layer_profile(
        samplePoint.x, samplePoint.y, samplePoint.z, layerParameters);
    const float shapeMask = profile > 1.0e-6f
        ? std::min(sampledDensity / profile, 1.0f)
        : 0.0f;
    if (shapeMask <= 0.0f) return result;

    // How much of the slab this ray crosses, relative to a vertical crossing: the geometric path
    // length through the layer. A grazing ray passes through more cloud, which is what makes the
    // layer thicken toward the horizon, and the clamp keeps a ray that never enters the slab from
    // reporting an infinite crossing.
    const float verticalCrossing = std::max(slabThickness, 1.0e-3f);
    const float slabPathLength = distanceToTop - distanceToBase;
    const float thicknessRatio = std::clamp(slabPathLength / verticalCrossing, 0.0f, 1.0f);
    result.pathLength = slabPathLength;
    const float geometricMask = saturate(shapeMask * thicknessRatio);

    const float density = std::max(parameters.cloudDensity, 0.0f);
    if (density <= 0.0f) return result;

    const glm::vec3 sun = sunDirection(parameters);
    // A sun at or below the horizon does not light the layer, so an evening layer fades out rather
    // than glowing from underneath.
    const float sunAboveHorizon = saturate(sun.y * 12.0f);
    const float phase = cloudPhase(glm::dot(direction, sun));
    const glm::vec3 keyLight = skyLightColor(parameters);
    const float keyStrength = std::max(parameters.sunIntensity, 0.0f) * sunAboveHorizon;

    // Ambient is the sky the cloud actually sits in, not the zenith.
    //
    // This is geometry, not a tuning constant. A cloud at 2 km is lit by the dense, bright air below
    // and around it: at the horizon the sky's own radiance is several times its zenith value, so a
    // cloud lit only by the zenith came out at 4% of the sky it was covering and read as a black
    // stain rather than a cloud. `ambientDirection` is tilted down towards the horizon and towards
    // whichever way the sun is, which is where the multiply-scattered light that lights a cloud's
    // underside comes from -- and it costs one sky evaluation, the same as before.
    const float ambientElevation = glm::radians(
        std::clamp(parameters.cloudAmbientElevationDegrees, 0.0f, 89.0f));
    const float sunAzimuth = std::atan2(sun.x, sun.z);
    const glm::vec3 ambientDirection(
        std::cos(ambientElevation) * std::sin(sunAzimuth),
        std::sin(ambientElevation),
        std::cos(ambientElevation) * std::cos(sunAzimuth)
    );
    const glm::vec3 ambientSky = scatteringRadiance(ambientDirection, parameters);

    const float horizonFadeDegrees = std::max(parameters.cloudHorizonFadeDegrees, 0.0f);
    const float elevationDegrees = glm::degrees(std::asin(std::clamp(direction.y, 0.0f, 1.0f)));
    const float horizonFade = horizonFadeDegrees <= 0.0f
        ? 1.0f
        : saturate(elevationDegrees / horizonFadeDegrees);

    result.mask = saturate(geometricMask * density * horizonFade);
    // The phase function peaks near 23 at the sun, so the key term is scaled well below the ambient
    // term: the silver lining should read as a bright rim, not as a hole burned through the layer.
    result.ambient = (keyLight * (0.08f * keyStrength * phase)
        + ambientSky * std::max(parameters.cloudAmbientScale, 0.0f)) * result.mask;
    result.minimumCameraHeight = base;
    return result;
}

glm::vec3 environmentRadiance(
    const glm::vec3& viewDirection,
    const AtmosphereParameters& parameters,
    float cameraHeight
) {
    if (!parameters.enabled) return glm::vec3(0.0f);
    const glm::vec3 sky = skyRadiance(viewDirection, parameters);
    const CloudLayer layer = cloudLayer(viewDirection, parameters, cameraHeight);
    if (layer.mask <= 0.0f) {
        // The sun disk stays separate from the sky so a caller that only wants the atmosphere can
        // skip it; a clear direction composes exactly as it did before clouds existed.
        return sky + sunDiskRadiance(viewDirection, parameters);
    }
    // Attenuate rather than replace: the sky and the disk stay behind the cloud, which is what
    // keeps the sun visible through a gap and stops the layer from reading as a decal.
    return sky * (1.0f - layer.mask)
        + sunDiskRadiance(viewDirection, parameters) * (1.0f - layer.mask)
        + layer.ambient;
}

float sunAngularRadiusDegrees() {
    return sunAngularRadius;
}

glm::vec3 sunIrradiance(const AtmosphereParameters& parameters) {
    if (!parameters.enabled) return glm::vec3(0.0f);
    const float cosRadius = std::cos(glm::radians(sunAngularRadius));
    const float solidAngle = 2.0f * pi * (1.0f - cosRadius);
    // The disk's radiance is uniform inside `sunAngularRadius`, so the irradiance on a surface
    // facing it is exactly radiance times solid angle.
    return sunDiskRadiance(sunDirection(parameters), parameters) * solidAngle;
}

std::vector<glm::vec3> generateEquirect(
    const AtmosphereParameters& parameters,
    int width,
    int height,
    float cameraHeight
) {
    std::vector<glm::vec3> pixels;
    if (width <= 0 || height <= 0) return pixels;
    pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        // Row 0 is the +Y pole, matching the file loader's convention.
        const float theta = pi * (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);
        for (int x = 0; x < width; ++x) {
            const float phi = 2.0f * pi * (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const glm::vec3 direction(
                sinTheta * std::sin(phi),
                cosTheta,
                sinTheta * std::cos(phi)
            );
            // `environmentRadiance` already resolves the lower hemisphere as the ground's reflected
            // radiance and leaves the sun disk out of it, so a ground texel cannot see the disk
            // even though the call no longer special-cases it here.
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                   + static_cast<std::size_t>(x)] =
                environmentRadiance(direction, parameters, cameraHeight);
        }
    }
    return pixels;
}

CloudTierBudget cloudTierBudget(CloudQualityTier tier) {
    CloudTierBudget budget;
    // The ends of the brief's measured ranges: 24..48 primary steps, 4..6 light steps. The low tier
    // is the bottom of the range rather than below it -- a budget under 24 steps puts the sample
    // spacing through a 4800-unit slab at over 200 units, which is coarser than the layer's own
    // finest feature and turns the march into a different function rather than a cheaper one.
    budget.primarySteps = tier == CloudQualityTier::High ? 48 : 24;
    budget.lightSteps = tier == CloudQualityTier::High ? 6 : 4;
    return budget;
}

void applyCloudPreset(AtmosphereParameters& parameters, CloudPreset preset) {
    // Heights use the atmosphere's scene units. The fixed-camera acceptance calibrates coverage
    // for the 3D field; these presets do not establish a physical conversion to metres.
    parameters.cloudsEnabled = true;
    switch (preset) {
    case CloudPreset::Cumulus:
        parameters.cloudBaseHeight = 3200.0f;
        parameters.cloudTopHeight = 8000.0f;
        parameters.cloudFeatureScale = 6400.0f;
        parameters.cloudCoverage = 0.60f;
        parameters.cloudDensity = 1.0f;
        parameters.cloudWeatherScale = 25600.0f;
        parameters.cloudCoverageVariation = 0.90f;
        parameters.cloudType = 0.80f;
        parameters.cloudTypeVariation = 0.45f;
        parameters.cloudHeightVariation = 0.30f;
        parameters.cloudDetailStrength = 0.45f;
        parameters.cloudDetailEdge = 0.15f;
        break;
    case CloudPreset::Stratus:
        // A shallow deck: a quarter of the cumulus thickness, wider features, more coverage and much
        // less weather contrast, which is what "even overcast with some breaks" is as numbers.
        parameters.cloudBaseHeight = 1200.0f;
        parameters.cloudTopHeight = 2600.0f;
        parameters.cloudFeatureScale = 9600.0f;
        parameters.cloudCoverage = 0.95f;
        parameters.cloudDensity = 0.85f;
        parameters.cloudWeatherScale = 38400.0f;
        parameters.cloudCoverageVariation = 0.50f;
        parameters.cloudType = 0.0f;
        parameters.cloudTypeVariation = 0.10f;
        parameters.cloudHeightVariation = 0.12f;
        parameters.cloudDetailStrength = 0.25f;
        parameters.cloudDetailEdge = 0.05f;
        break;
    case CloudPreset::Cirrus:
        // Thin and high. `cloudTypeVariation` is zero because a cirrus sheet is one texture across
        // the whole sky -- letting the weather map swing its type would make some of it convective,
        // which at this thickness reads as a rendering artifact rather than as weather.
        parameters.cloudBaseHeight = 12000.0f;
        parameters.cloudTopHeight = 14500.0f;
        parameters.cloudFeatureScale = 16000.0f;
        parameters.cloudCoverage = 0.35f;
        parameters.cloudDensity = 0.30f;
        parameters.cloudWeatherScale = 64000.0f;
        parameters.cloudCoverageVariation = 0.80f;
        parameters.cloudType = 0.0f;
        parameters.cloudTypeVariation = 0.0f;
        parameters.cloudHeightVariation = 0.10f;
        parameters.cloudDetailStrength = 0.35f;
        parameters.cloudDetailEdge = 0.05f;
        break;
    }
}

} // namespace atmosphere
