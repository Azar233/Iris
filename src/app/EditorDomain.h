#pragma once

// Domain snapshots for the Renderer Inspector (P1-0A A2b2b).
//
// The Inspector edits a copy of one domain, submits a single typed
// `EditorCommand`, and `Application::processEditorCommands()` owns validation and
// application. These helpers are the single definition of the
// RendererSettings -> payload mapping, so the Inspector, the loop that restores
// editor state and the automated interaction regression cannot drift apart.
//
// Capture clamps every numeric field into the range its Inspector control can
// produce. `.myscene` files are read without clamping, so a hand-edited or legacy
// value outside the range would otherwise make its whole domain permanently
// uneditable: the entry point rejects out-of-range payloads, and every capture
// would keep re-sending the offending value. Normalising on capture makes the
// round trip closed under the documented range and heals such a scene on the next
// edit of that domain.
//
// Applying a payload is deliberately *not* provided here: the command entry point
// validates and clamps every field, and a wholesale copy would bypass that.

#include <algorithm>

#include "app/EditorSession.h"
#include "render/Renderer.h"

namespace EditorDomain {

inline glm::vec3 clampColor(const EditorVector3Payload& color) {
    return glm::vec3(
        std::clamp(color.x, 0.0f, 1.0f),
        std::clamp(color.y, 0.0f, 1.0f),
        std::clamp(color.z, 0.0f, 1.0f)
    );
}

inline EditorVector3Payload clampedColor(const glm::vec3& color) {
    EditorVector3Payload payload;
    payload.x = std::clamp(color.x, 0.0f, 1.0f);
    payload.y = std::clamp(color.y, 0.0f, 1.0f);
    payload.z = std::clamp(color.z, 0.0f, 1.0f);
    return payload;
}

inline EditorStageSettingsPayload captureStageSettings(
    bool groundReceiver,
    const glm::vec3& groundColor,
    float groundOffset,
    bool comparisonObject
) {
    EditorStageSettingsPayload snapshot;
    snapshot.groundReceiver = groundReceiver;
    snapshot.groundColor = clampedColor(groundColor);
    snapshot.groundOffset = std::clamp(groundOffset, -3.0f, 0.0f);
    snapshot.comparisonObject = comparisonObject;
    return snapshot;
}

inline EditorPbrEnvironmentSettingsPayload capturePbrEnvironmentSettings(
    const RendererSettings& settings
) {
    EditorPbrEnvironmentSettingsPayload snapshot;
    snapshot.pbrEnabled = settings.pbrEnabled;
    snapshot.iblEnabled = settings.iblEnabled;
    snapshot.skyboxEnabled = settings.skyboxEnabled;
    snapshot.shadowsEnabled = settings.shadowsEnabled;
    // Cascades are normalised at the capture layer so a hand-edited `.myscene` cannot lock the whole
    // PBR/environment domain out with an out-of-range cascade count.
    snapshot.shadowCascadeCount = std::clamp(settings.shadowCascadeCount, 1, 4);
    snapshot.shadowCascadeSplitLambda =
        std::clamp(settings.shadowCascadeSplitLambda, 0.0f, 1.0f);
    snapshot.shadowCascadeDebugView = settings.shadowCascadeDebugView;
    snapshot.coloredTransmissionShadowsEnabled =
        settings.coloredTransmissionShadowsEnabled;
    snapshot.environmentIntensity = std::clamp(settings.environmentIntensity, 0.0f, 2.0f);
    return snapshot;
}

inline EditorWaterSettingsPayload captureWaterSettings(const RendererSettings& settings) {
    EditorWaterSettingsPayload snapshot;
    snapshot.enabled = settings.water.enabled;
    snapshot.preset = static_cast<int>(settings.water.preset);
    snapshot.quality = static_cast<int>(settings.water.quality);
    snapshot.level = std::clamp(settings.water.level, -10.0f, 10.0f);
    snapshot.extent = std::clamp(settings.water.extent, 20.0f, 2000.0f);
    snapshot.amplitude = std::clamp(settings.water.amplitude, 0.0f, 2.0f);
    snapshot.speed = std::clamp(settings.water.speed, 0.0f, 5.0f);
    snapshot.steepness = std::clamp(settings.water.steepness, 0.0f, 0.9f);
    snapshot.foamStrength = std::clamp(settings.water.foamStrength, 0.0f, 1.0f);
    snapshot.roughness = std::clamp(settings.water.roughness, 0.02f, 0.8f);
    snapshot.reflectionStrength = std::clamp(settings.water.reflectionStrength, 0.0f, 2.0f);
    snapshot.rippleStrength = std::clamp(settings.water.rippleStrength, 0.0f, 1.0f);
    snapshot.sunGlintStrength = std::clamp(settings.water.sunGlintStrength, 0.0f, 2.0f);
    snapshot.windX = std::clamp(settings.water.windDirection.x, -1.0f, 1.0f);
    snapshot.windZ = std::clamp(settings.water.windDirection.y, -1.0f, 1.0f);
    return snapshot;
}

inline EditorShadingSettingsPayload captureShadingSettings(const RendererSettings& settings) {
    EditorShadingSettingsPayload snapshot;
    snapshot.shadingMode = static_cast<int>(settings.shadingMode);
    snapshot.renderPath = static_cast<int>(settings.renderPath);
    snapshot.gBufferDebugView = static_cast<int>(settings.gBufferDebugView);
    snapshot.stylizedBandCount = std::clamp(settings.stylizedBandCount, 2, 8);
    snapshot.stylizedBandSoftness = std::clamp(settings.stylizedBandSoftness, 0.0f, 0.25f);
    snapshot.stylizedSpecularSize = std::clamp(settings.stylizedSpecularSize, 0.02f, 0.8f);
    snapshot.stylizedSpecularSoftness =
        std::clamp(settings.stylizedSpecularSoftness, 0.0f, 0.2f);
    snapshot.stylizedRimWidth = std::clamp(settings.stylizedRimWidth, 0.02f, 0.9f);
    snapshot.stylizedRimSoftness = std::clamp(settings.stylizedRimSoftness, 0.0f, 0.3f);
    snapshot.stylizedRimIntensity = std::clamp(settings.stylizedRimIntensity, 0.0f, 3.0f);
    snapshot.stylizedShadowTint = clampedColor(settings.stylizedShadowTint);
    snapshot.stylizedRimColor = clampedColor(settings.stylizedRimColor);
    snapshot.stylizedOutlineEnabled = settings.stylizedOutlineEnabled;
    snapshot.stylizedOutlineWidth = std::clamp(settings.stylizedOutlineWidth, 0.5f, 6.0f);
    snapshot.stylizedOutlineDepthThreshold =
        std::clamp(settings.stylizedOutlineDepthThreshold, 0.001f, 0.12f);
    snapshot.stylizedOutlineNormalThreshold =
        std::clamp(settings.stylizedOutlineNormalThreshold, 0.02f, 0.8f);
    snapshot.stylizedOutlineColor = clampedColor(settings.stylizedOutlineColor);
    snapshot.stylizedDitherEnabled = settings.stylizedDitherEnabled;
    snapshot.stylizedDitherStrength = std::clamp(settings.stylizedDitherStrength, 0.0f, 1.0f);
    snapshot.stylizedHeightFogEnabled = settings.stylizedHeightFogEnabled;
    snapshot.stylizedHeightFogDensity =
        std::clamp(settings.stylizedHeightFogDensity, 0.0f, 2.0f);
    snapshot.stylizedHeightFogBaseHeight =
        std::clamp(settings.stylizedHeightFogBaseHeight, -10.0f, 10.0f);
    snapshot.stylizedHeightFogFalloff =
        std::clamp(settings.stylizedHeightFogFalloff, 0.01f, 4.0f);
    snapshot.stylizedHeightFogColor = clampedColor(settings.stylizedHeightFogColor);
    snapshot.stylizedColorGradingEnabled = settings.stylizedColorGradingEnabled;
    snapshot.stylizedColorGradingLut =
        static_cast<int>(settings.stylizedColorGradingLut);
    snapshot.stylizedColorGradingStrength =
        std::clamp(settings.stylizedColorGradingStrength, 0.0f, 1.0f);
    snapshot.stylizedDebugView = static_cast<int>(settings.stylizedDebugView);
    return snapshot;
}

inline EditorPostProcessingSettingsPayload capturePostProcessingSettings(
    const RendererSettings& settings
) {
    EditorPostProcessingSettingsPayload snapshot;
    snapshot.ssaoEnabled = settings.ssaoEnabled;
    snapshot.ssaoRadius = std::clamp(settings.ssaoRadius, 0.05f, 2.0f);
    snapshot.ssaoBias = std::clamp(settings.ssaoBias, 0.0f, 0.15f);
    snapshot.ssaoStrength = std::clamp(settings.ssaoStrength, 0.1f, 3.0f);
    snapshot.temporalAaEnabled = settings.temporalAaEnabled;
    snapshot.temporalHistoryWeight =
        std::clamp(settings.temporalHistoryWeight, 0.0f, 0.98f);
    snapshot.temporalDebugView = std::clamp(settings.temporalDebugView, 0, 2);
    snapshot.toneMapping = settings.toneMapping;
    snapshot.bloom = settings.bloom;
    snapshot.bloomThreshold = std::clamp(settings.bloomThreshold, 0.1f, 4.0f);
    snapshot.bloomIntensity = std::clamp(settings.bloomIntensity, 0.0f, 1.0f);
    snapshot.exposure = std::clamp(settings.exposure, 0.1f, 4.0f);
    return snapshot;
}

inline EditorRasterizationSettingsPayload captureRasterizationSettings(
    const RendererSettings& settings
) {
    EditorRasterizationSettingsPayload snapshot;
    snapshot.wireframe = settings.wireframe;
    snapshot.cullBackFaces = settings.cullBackFaces;
    snapshot.normalMapping = settings.normalMapping;
    snapshot.showGrid = settings.showGrid;
    snapshot.showAxes = settings.showAxes;
    snapshot.backgroundColor = clampedColor(settings.backgroundColor);
    snapshot.msaaSamples = settings.msaaSamples > 1 ? 4 : 1;
    return snapshot;
}

inline EditorCameraSettingsPayload captureCameraSettings(const Camera& camera) {
    EditorCameraSettingsPayload snapshot;
    snapshot.fieldOfViewDegrees = std::clamp(camera.fieldOfView(), 15.0f, 90.0f);
    snapshot.farPlane = std::clamp(camera.farPlane(), 100.0f, 2000.0f);
    return snapshot;
}

inline EditorRuntimeSettingsPayload captureRuntimeSettings(
    bool vsync,
    const RendererSettings& settings
) {
    EditorRuntimeSettingsPayload snapshot;
    snapshot.vsync = vsync;
    snapshot.shaderHotReloadEnabled = settings.shaderHotReloadEnabled;
    return snapshot;
}

inline EditorGlassSettingsPayload captureGlassSettings(const RendererSettings& settings) {
    EditorGlassSettingsPayload snapshot;
    snapshot.transmissionEnabled = settings.transmissionEnabled;
    snapshot.dispersionEnabled = settings.dispersionEnabled;
    snapshot.geometricThicknessEnabled = settings.geometricThicknessEnabled;
    snapshot.twoInterfaceRefractionEnabled = settings.twoInterfaceRefractionEnabled;
    snapshot.refractionScale = std::clamp(settings.refractionScale, 0.0f, 0.8f);
    snapshot.refractionSteps = std::clamp(settings.refractionSteps, 4, 32);
    snapshot.volumeThicknessScale = std::clamp(settings.volumeThicknessScale, 0.0f, 4.0f);
    snapshot.volumeGlassOverrideEnabled = settings.volumeGlassOverrideEnabled;
    snapshot.volumeGlassTransmission =
        std::clamp(settings.volumeGlassTransmission, 0.0f, 1.0f);
    snapshot.volumeGlassRoughness = std::clamp(settings.volumeGlassRoughness, 0.04f, 1.0f);
    snapshot.volumeGlassAttenuationColor =
        clampedColor(settings.volumeGlassAttenuationColor);
    snapshot.volumeGlassAttenuationDistance =
        std::clamp(settings.volumeGlassAttenuationDistance, 0.05f, 8.0f);
    snapshot.dispersionStrength = std::clamp(settings.dispersionStrength, 0.0f, 2.5f);
    snapshot.glassDebugView = static_cast<int>(settings.glassDebugView);
    if (snapshot.glassDebugView < 0 || snapshot.glassDebugView > 12) {
        snapshot.glassDebugView = 0;
    }
    return snapshot;
}

inline EditorCausticsSettingsPayload captureCausticsSettings(const RendererSettings& settings) {
    EditorCausticsSettingsPayload snapshot;
    snapshot.causticsEnabled = settings.causticsEnabled;
    snapshot.causticsMode = static_cast<int>(settings.causticsMode);
    snapshot.causticsStrength = std::clamp(settings.causticsStrength, 0.0f, 8.0f);
    snapshot.causticsScale = std::clamp(settings.causticsScale, 0.1f, 3.0f);
    snapshot.causticsDirection = {
        std::clamp(settings.causticsDirection.x, -1.5f, 1.5f),
        std::clamp(settings.causticsDirection.y, -1.5f, 1.5f),
        std::clamp(settings.causticsDirection.z, -1.5f, 1.5f)
    };
    snapshot.causticsSharpness = std::clamp(settings.causticsSharpness, 0.0f, 1.0f);
    snapshot.causticsAnimated = settings.causticsAnimated;
    return snapshot;
}

inline EditorInstanceSettingsPayload captureInstanceSettings(const RendererSettings& settings) {
    EditorInstanceSettingsPayload snapshot;
    snapshot.instanceOptimizationEnabled = settings.instanceOptimizationEnabled;
    snapshot.frustumCullingEnabled = settings.frustumCullingEnabled;
    snapshot.lodSelectionEnabled = settings.lodSelectionEnabled;
    return snapshot;
}

// Captures the sky state the Inspector edits. Normalising here (rather than rejecting) keeps the
// command valid even when a caller hands over a scene file that stored an out-of-range value.
inline EditorAtmosphereSettingsPayload captureAtmosphereSettings(const RendererSettings& settings) {
    EditorAtmosphereSettingsPayload snapshot;
    snapshot.enabled = settings.atmosphere.enabled;
    snapshot.sunElevationDegrees =
        std::clamp(settings.atmosphere.sunElevationDegrees, -10.0f, 90.0f);
    snapshot.sunAzimuthDegrees =
        std::clamp(settings.atmosphere.sunAzimuthDegrees, 0.0f, 360.0f);
    snapshot.turbidity = std::clamp(settings.atmosphere.turbidity, 0.0f, 10.0f);
    snapshot.skyIntensity = std::clamp(settings.atmosphere.skyIntensity, 0.0f, 20.0f);
    snapshot.sunIntensity = std::clamp(settings.atmosphere.sunIntensity, 0.0f, 8.0f);
    snapshot.groundAlbedo = std::clamp(settings.atmosphere.groundAlbedo, 0.0f, 1.0f);
    snapshot.nightSkyEnabled = settings.atmosphere.nightSkyEnabled;
    snapshot.moonIntensity = std::clamp(settings.atmosphere.moonIntensity, 0.0f, 4.0f);
    snapshot.starIntensity = std::clamp(settings.atmosphere.starIntensity, 0.0f, 4.0f);
    // Aerial perspective is part of the atmosphere domain because it is the same air, integrated
    // between the camera and the geometry. The scale height is in world units, so its ceiling is
    // generous: a scene may model metres, kilometres or something arbitrary.
    snapshot.aerialPerspectiveEnabled = settings.atmosphere.aerialPerspectiveEnabled;
    snapshot.aerialPerspectiveStrength =
        std::clamp(settings.atmosphere.aerialPerspectiveStrength, 0.0f, 4.0f);
    snapshot.aerialPerspectiveScaleHeight =
        std::clamp(settings.atmosphere.aerialPerspectiveScaleHeight, 0.01f, 20000.0f);
    // The cloud layer rides the same domain and the same world-unit convention as the sky it is
    // composited into. Heights are clamped in metres-like world units; the coverage and density
    // ranges match the Inspector sliders so "capture -> submit -> apply" closes on legal values.
    snapshot.cloudsEnabled = settings.atmosphere.cloudsEnabled;
    snapshot.cloudBaseHeight = std::clamp(settings.atmosphere.cloudBaseHeight, 0.0f, 20000.0f);
    snapshot.cloudTopHeight = std::clamp(settings.atmosphere.cloudTopHeight, 0.0f, 30000.0f);
    snapshot.cloudCoverage = std::clamp(settings.atmosphere.cloudCoverage, 0.0f, 1.0f);
    snapshot.cloudDensity = std::clamp(settings.atmosphere.cloudDensity, 0.0f, 4.0f);
    snapshot.cloudWindOffsetX = std::clamp(settings.atmosphere.cloudWindOffsetX, -60000.0f, 60000.0f);
    snapshot.cloudWindOffsetZ = std::clamp(settings.atmosphere.cloudWindOffsetZ, -60000.0f, 60000.0f);
    snapshot.cloudFeatureScale = std::clamp(settings.atmosphere.cloudFeatureScale, 10.0f, 200000.0f);
    snapshot.cloudNoisePeriod = std::clamp(settings.atmosphere.cloudNoisePeriod, 1.0f, 16.0f);
    // The weather map's controls, clamped to the ranges the Inspector sliders use. The detail
    // weight is a fraction of the local density, so 0..1 is the whole legal range rather than a
    // chosen ceiling.
    snapshot.cloudWeatherScale = std::clamp(settings.atmosphere.cloudWeatherScale, 100.0f, 400000.0f);
    snapshot.cloudCoverageVariation =
        std::clamp(settings.atmosphere.cloudCoverageVariation, 0.0f, 1.0f);
    snapshot.cloudType = std::clamp(settings.atmosphere.cloudType, 0.0f, 1.0f);
    snapshot.cloudTypeVariation = std::clamp(settings.atmosphere.cloudTypeVariation, 0.0f, 1.0f);
    snapshot.cloudHeightVariation = std::clamp(settings.atmosphere.cloudHeightVariation, 0.0f, 1.0f);
    snapshot.cloudDetailStrength = std::clamp(settings.atmosphere.cloudDetailStrength, 0.0f, 1.0f);
    snapshot.cloudDetailEdge = std::clamp(settings.atmosphere.cloudDetailEdge, 0.0f, 1.0f);
    snapshot.cloudHalfResolution = settings.atmosphere.cloudHalfResolution;
    snapshot.cloudTemporalEnabled = settings.atmosphere.cloudTemporalEnabled;
    snapshot.cloudShadowsEnabled = settings.atmosphere.cloudShadowsEnabled;
    snapshot.cloudGodRaysEnabled = settings.atmosphere.cloudGodRaysEnabled;
    snapshot.cloudDeterministic = settings.atmosphere.cloudDeterministic;
    snapshot.cloudOfflineNoise = settings.atmosphere.cloudOfflineNoise;
    snapshot.cloudGodRaysStrength = settings.atmosphere.cloudGodRaysStrength;
    snapshot.cloudQuality = settings.atmosphere.cloudQuality == atmosphere::CloudQualityTier::High
        ? 1 : 0;
    snapshot.cloudHorizonFadeDegrees =
        std::clamp(settings.atmosphere.cloudHorizonFadeDegrees, 0.0f, 30.0f);
    snapshot.cloudAmbientElevationDegrees =
        std::clamp(settings.atmosphere.cloudAmbientElevationDegrees, 0.0f, 89.0f);
    snapshot.cloudAmbientScale = std::clamp(settings.atmosphere.cloudAmbientScale, 0.0f, 4.0f);
    return snapshot;
}

// Applies a cloud preset to an Inspector snapshot.
//
// The preset is defined on `atmosphere::AtmosphereParameters`, because that is what a scene and a
// headless job carry; the Inspector edits a *payload* with the same fields and slightly wider
// clamping. Rather than define the preset twice, this builds the parameters, applies the preset
// there, and copies the cloud fields across through the same clamping `captureAtmosphereSettings`
// uses. The two mappings could in principle drift; keeping them in one file, adjacent, is what makes
// that a compile-visible problem rather than a wrong number in a slider.
inline void applyCloudPreset(
    EditorAtmosphereSettingsPayload& snapshot,
    atmosphere::CloudPreset preset
) {
    atmosphere::AtmosphereParameters parameters;
    parameters.enabled = true;
    // Start from what the snapshot already carries so a preset never silently reverts a sun or a
    // sky the user set; `applyCloudPreset` only writes cloud members.
    parameters.cloudsEnabled = snapshot.cloudsEnabled;
    parameters.cloudQuality = snapshot.cloudQuality == 1
        ? atmosphere::CloudQualityTier::High : atmosphere::CloudQualityTier::Low;
    atmosphere::applyCloudPreset(parameters, preset);
    snapshot.cloudsEnabled = parameters.cloudsEnabled;
    snapshot.cloudBaseHeight = std::clamp(parameters.cloudBaseHeight, 0.0f, 20000.0f);
    snapshot.cloudTopHeight = std::clamp(parameters.cloudTopHeight, 0.0f, 30000.0f);
    snapshot.cloudCoverage = std::clamp(parameters.cloudCoverage, 0.0f, 1.0f);
    snapshot.cloudDensity = std::clamp(parameters.cloudDensity, 0.0f, 4.0f);
    snapshot.cloudFeatureScale = std::clamp(parameters.cloudFeatureScale, 10.0f, 200000.0f);
    snapshot.cloudWeatherScale = std::clamp(parameters.cloudWeatherScale, 100.0f, 400000.0f);
    snapshot.cloudCoverageVariation =
        std::clamp(parameters.cloudCoverageVariation, 0.0f, 1.0f);
    snapshot.cloudType = std::clamp(parameters.cloudType, 0.0f, 1.0f);
    snapshot.cloudTypeVariation = std::clamp(parameters.cloudTypeVariation, 0.0f, 1.0f);
    snapshot.cloudHeightVariation = std::clamp(parameters.cloudHeightVariation, 0.0f, 1.0f);
    snapshot.cloudDetailStrength = std::clamp(parameters.cloudDetailStrength, 0.0f, 1.0f);
    snapshot.cloudDetailEdge = std::clamp(parameters.cloudDetailEdge, 0.0f, 1.0f);
}

} // namespace EditorDomain
