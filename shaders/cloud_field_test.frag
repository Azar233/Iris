#version 330 core

// GPU side of the cloud density parity check (P1-A slice 6, step C2; extended for the weather map
// and the base/detail split in step C5).
//
// This shader answers one question before any volumetric work is built on it: does the field in
// `src/optics/CloudField.h`, compiled by the GPU driver, produce the same numbers as the same file
// compiled by the C++ compiler? The project's approach to cloud correctness assumes the CPU
// reference and the GPU pass can read one shared source, so that assumption is measured here rather
// than hoped for.
//
// The concern is specific. The field is built on 32-bit integer hashing with multiplies and shifts,
// and GLSL only guarantees 16 bits of precision for `highp uint` in the specification even though
// every desktop driver in practice provides 32. If a driver truncated the intermediates, the two
// sides would diverge and no care in the marcher would fix it.
//
// Channel layout, fixed so the test's expectations and this file cannot drift apart:
//   outField    = (hashUnit, worley, baseShape, detailShape) at the tile inputs
//   outWeather  = (weather red, green, blue, profile) at the weather tile and world height
//   outDensity  = (taper, localCoverage, density, baseCloud) at the world inputs
//
// The weather and split probes exist because "the density differs" is not a diagnosis. C5 added
// three new sampled fields and an erosion step in front of the threshold, and each of them can drift
// between the two compilers on its own; reporting them separately is what keeps a one-line
// disagreement from turning into another search through the marcher.
//
// Everything here is a pure function of `vUv` and the declared uniforms. An earlier revision also
// reported differences against CPU-supplied uniforms, which made the shader's outputs depend on
// whether the caller had set them -- a state that produced several misleading measurements before it
// was removed.

// The shared body marks its functions `MYRENDERER_CLOUD_INLINE`. The C++ adapter defines that as
// `inline` so several translation units can include the same body; GLSL has no `inline` keyword and
// rejects it, so here it is defined as empty. See `src/optics/CloudFieldCpp.h` for the other half.
#define MYRENDERER_CLOUD_INLINE

#include "cloud_noise_sample.glsl"
#include "src/optics/CloudField.h"

in vec2 vUv;

uniform vec3 uTile;        // (tileX, tileY, tileSpan) -- the span is swept across the fragment grid
uniform vec3 uWeatherTile; // (weatherX, weatherY, span) -- in weather-map tiles, not cloud tiles
uniform int uPeriod;
uniform vec3 uWorld;       // (worldX, worldZ, span) -- both swept across the fragment grid
uniform float uBaseHeight;
uniform float uTopHeight;
uniform float uFeatureScale;
uniform int uNoisePeriod;
uniform bool uOfflineNoise;
uniform float uWeatherScale;
uniform vec2 uWind;        // (windX, windZ)
uniform float uCoverage;
uniform float uDensityScale;
uniform float uCoverageVariation;
uniform float uCloudType;
uniform float uTypeVariation;
uniform float uHeightVariation;
uniform float uDetailStrength;
uniform float uDetailEdge;
uniform float uShapeBlend;

out vec4 outField;
out vec4 outWeather;
out vec4 outDensity;

void main() {
    // Everything is derived from the fragment coordinate so one full-screen draw covers a grid of
    // samples instead of one pixel: the test wants a spread of points, not a single value.
    float tileX = uTile.x + vUv.x * uTile.z;
    float tileY = uTile.y + vUv.y * uTile.z;

    float hashValue = myrenderer_cloud_hash_unit(
        int(floor(tileX * 64.0)), int(floor(tileY * 64.0)));
    outField = vec4(
        hashValue,
        myrenderer_cloud_worley(tileX, tileY, uPeriod),
        myrenderer_cloud_base_shape(tileX, tileY, uNoisePeriod),
        myrenderer_cloud_detail_shape(tileX, tileY, uNoisePeriod));

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
    layer.shapeBlend = uShapeBlend;

    float weatherX = uWeatherTile.x + vUv.x * uWeatherTile.z;
    float weatherY = uWeatherTile.y + vUv.y * uWeatherTile.z;

    // The density's own inputs and intermediate steps, reported alongside its result. Checking them
    // separately is what turns "the density differs" into a statement about which step differs: the
    // weather sample, the height profile, the coverage remap or the erosion.
    float worldX = uWorld.x + vUv.x * uWorld.z;
    float worldZ = uWorld.y + vUv.y * uWorld.z;
    float worldY = mix(uBaseHeight, uTopHeight, vUv.y);
    float slab = max(uTopHeight - uBaseHeight, 1.0e-3);
    float taper = (worldY - uBaseHeight) / slab;

    float profileValue = myrenderer_cloud_layer_profile(worldX, worldY, worldZ, layer);
    outWeather = vec4(
        myrenderer_cloud_weather(weatherX, weatherY, 0),
        myrenderer_cloud_weather(weatherX, weatherY, 1),
        myrenderer_cloud_weather(weatherX, weatherY, 2),
        profileValue);

    // The world position's own weather sample, which is the one the density actually consumed: the
    // swept `uWeatherTile` grid above is a separate probe of the map, and a wrap bug in the world
    // mapping would not show up in it.
    float densityWeatherX = (worldX + layer.windX) / max(uWeatherScale, 1.0e-3);
    float densityWeatherY = (worldZ + layer.windZ) / max(uWeatherScale, 1.0e-3);
    float localCoverage = clamp(
        uCoverage + (myrenderer_cloud_weather(densityWeatherX, densityWeatherY, 0) - 0.5)
            * uCoverageVariation, 0.0, 1.0);
    float warpedTileX = (worldX + uWind.x) / max(uFeatureScale, 1.0e-3)
        + myrenderer_cloud_height_warp_x(taper) * 0.15;
    float warpedTileY = (worldZ + uWind.y) / max(uFeatureScale, 1.0e-3)
        + myrenderer_cloud_height_warp_y(taper) * 0.15;
    float baseCloud = localCoverage > 0.0
        ? clamp((myrenderer_cloud_volume_base_shape(
            warpedTileX, warpedTileY, taper, uNoisePeriod) * profileValue
            - (1.0 - localCoverage)) / localCoverage, 0.0, 1.0)
        : 0.0;
    outDensity = vec4(
        taper,
        localCoverage,
        myrenderer_cloud_density(worldX, worldY, worldZ, layer),
        baseCloud);
}
