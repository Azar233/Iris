#include "asset/InputManifest.h"
#include "scene/SceneDocument.h"
#include "optics/CloudNoiseVolume.h"
#include "optics/CloudLightingLut.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

// Assimp also embeds RapidJSON, but builds it with its own configuration. Keep
// the scene-document parser in a private namespace so MinGW cannot merge the
// two libraries' inline COMDAT symbols and corrupt RapidJSON calls at runtime.
#define RAPIDJSON_NAMESPACE myrenderer_scene_json
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#undef RAPIDJSON_NAMESPACE
#undef RAPIDJSON_NAMESPACE_BEGIN
#undef RAPIDJSON_NAMESPACE_END

namespace scene_json = myrenderer_scene_json;

namespace {

using Writer = scene_json::PrettyWriter<scene_json::StringBuffer>;

void writeVec3(Writer& writer, const glm::vec3& value) {
    writer.StartArray();
    writer.Double(value.x); writer.Double(value.y); writer.Double(value.z);
    writer.EndArray();
}

void writeMatrix(Writer& writer, const glm::mat4& value) {
    writer.StartArray();
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) writer.Double(value[column][row]);
    }
    writer.EndArray();
}

void writeRendererSettings(Writer& writer, const RendererSettings& settings) {
    writer.StartObject();
#define WRITE_FLOAT(field) writer.Key(#field); writer.Double(settings.field)
#define WRITE_INT(field) writer.Key(#field); writer.Int(settings.field)
#define WRITE_BOOL(field) writer.Key(#field); writer.Bool(settings.field)
#define WRITE_VEC3(field) writer.Key(#field); writeVec3(writer, settings.field)
    WRITE_VEC3(backgroundColor); WRITE_VEC3(baseColor); WRITE_VEC3(lightDirection);
    WRITE_FLOAT(ambientStrength); WRITE_FLOAT(diffuseStrength); WRITE_FLOAT(specularStrength);
    WRITE_FLOAT(shininess); WRITE_INT(msaaSamples);
    writer.Key("renderPath"); writer.Int(static_cast<int>(settings.renderPath));
    writer.Key("shadingMode"); writer.Int(static_cast<int>(settings.shadingMode));
    writer.Key("stylizedPreset"); writer.Int(static_cast<int>(settings.stylizedPreset));
    writer.Key("gBufferDebugView"); writer.Int(static_cast<int>(settings.gBufferDebugView));
    WRITE_BOOL(wireframe); WRITE_BOOL(cullBackFaces); WRITE_BOOL(normalMapping);
    WRITE_BOOL(showGrid); WRITE_BOOL(showAxes); WRITE_BOOL(pbrEnabled); WRITE_BOOL(iblEnabled);
    WRITE_INT(stylizedBandCount); WRITE_FLOAT(stylizedBandSoftness);
    WRITE_FLOAT(stylizedSpecularSize); WRITE_FLOAT(stylizedSpecularSoftness);
    WRITE_FLOAT(stylizedRimWidth); WRITE_FLOAT(stylizedRimSoftness);
    WRITE_FLOAT(stylizedRimIntensity); WRITE_VEC3(stylizedShadowTint);
    WRITE_VEC3(stylizedRimColor);
    WRITE_BOOL(stylizedOutlineEnabled); WRITE_FLOAT(stylizedOutlineWidth);
    WRITE_FLOAT(stylizedOutlineDepthThreshold); WRITE_FLOAT(stylizedOutlineNormalThreshold);
    WRITE_VEC3(stylizedOutlineColor);
    WRITE_BOOL(stylizedDitherEnabled); WRITE_FLOAT(stylizedDitherStrength);
    WRITE_BOOL(stylizedHeightFogEnabled); WRITE_FLOAT(stylizedHeightFogDensity);
    WRITE_FLOAT(stylizedHeightFogBaseHeight); WRITE_FLOAT(stylizedHeightFogFalloff);
    WRITE_VEC3(stylizedHeightFogColor);
    WRITE_BOOL(stylizedColorGradingEnabled);
    writer.Key("stylizedColorGradingLut");
    writer.Int(static_cast<int>(settings.stylizedColorGradingLut));
    WRITE_FLOAT(stylizedColorGradingStrength);
    writer.Key("stylizedDebugView"); writer.Int(static_cast<int>(settings.stylizedDebugView));
    WRITE_BOOL(shadowsEnabled); WRITE_BOOL(coloredTransmissionShadowsEnabled);
    // Cascaded shadows. Defaults match `RendererSettings`, so a scene written before these fields
    // existed keeps the cascade count and split blend it was rendered with.
    writer.Key("shadowCascadeCount"); writer.Int(std::clamp(settings.shadowCascadeCount, 1, 4));
    writer.Key("shadowCascadeSplitLambda");
    writer.Double(std::clamp(settings.shadowCascadeSplitLambda, 0.0f, 1.0f));
    WRITE_BOOL(shadowCascadeDebugView);
    writer.Key("waterEnabled"); writer.Bool(settings.water.enabled);
    writer.Key("waterPreset"); writer.Int(static_cast<int>(settings.water.preset));
    writer.Key("waterQuality"); writer.Int(static_cast<int>(settings.water.quality));
    writer.Key("waterLevel"); writer.Double(settings.water.level);
    writer.Key("waterExtent"); writer.Double(settings.water.extent);
    writer.Key("waterAmplitude"); writer.Double(settings.water.amplitude);
    writer.Key("waterSpeed"); writer.Double(settings.water.speed);
    writer.Key("waterSteepness"); writer.Double(settings.water.steepness);
    writer.Key("waterFoamStrength"); writer.Double(settings.water.foamStrength);
    writer.Key("waterRoughness"); writer.Double(settings.water.roughness);
    writer.Key("waterReflectionStrength"); writer.Double(settings.water.reflectionStrength);
    writer.Key("waterRippleStrength"); writer.Double(settings.water.rippleStrength);
    writer.Key("waterSunGlintStrength"); writer.Double(settings.water.sunGlintStrength);
    writer.Key("waterWindX"); writer.Double(settings.water.windDirection.x);
    writer.Key("waterWindZ"); writer.Double(settings.water.windDirection.y);
    WRITE_BOOL(causticsEnabled);
    writer.Key("causticsMode"); writer.Int(static_cast<int>(settings.causticsMode));
    WRITE_FLOAT(causticsStrength); WRITE_FLOAT(causticsScale); WRITE_VEC3(causticsDirection);
    WRITE_FLOAT(causticsSharpness); WRITE_BOOL(causticsAnimated);
    WRITE_FLOAT(causticsReceiverPlaneY); WRITE_BOOL(transmissionEnabled);
    WRITE_BOOL(skyboxEnabled); WRITE_BOOL(toneMapping); WRITE_BOOL(bloom);
    WRITE_BOOL(showPrismIncidentBeam); WRITE_FLOAT(environmentIntensity);
    // Sun-driven analytic sky. Off by default, so a scene written before these fields existed
    // keeps its HDR environment.
    writer.Key("atmosphereEnabled"); writer.Bool(settings.atmosphere.enabled);
    writer.Key("sunElevationDegrees"); writer.Double(settings.atmosphere.sunElevationDegrees);
    writer.Key("sunAzimuthDegrees"); writer.Double(settings.atmosphere.sunAzimuthDegrees);
    writer.Key("skyTurbidity"); writer.Double(settings.atmosphere.turbidity);
    writer.Key("skyIntensity"); writer.Double(settings.atmosphere.skyIntensity);
    writer.Key("sunIntensity"); writer.Double(settings.atmosphere.sunIntensity);
    writer.Key("groundAlbedo"); writer.Double(settings.atmosphere.groundAlbedo);
    writer.Key("nightSkyEnabled"); writer.Bool(settings.atmosphere.nightSkyEnabled);
    writer.Key("moonIntensity"); writer.Double(settings.atmosphere.moonIntensity);
    writer.Key("starIntensity"); writer.Double(settings.atmosphere.starIntensity);
    // Aerial perspective is part of the same sky description; the distance field is in world
    // units, not metres, so a scene keeps its own unit scale.
    writer.Key("aerialPerspectiveEnabled"); writer.Bool(settings.atmosphere.aerialPerspectiveEnabled);
    writer.Key("aerialPerspectiveStrength"); writer.Double(settings.atmosphere.aerialPerspectiveStrength);
    writer.Key("aerialPerspectiveScaleHeight"); writer.Double(settings.atmosphere.aerialPerspectiveScaleHeight);
    // The cloud layer is part of the same sky description, in the same world units. Every field
    // round-trips so a cloud-covered scene reopens with the exact layer it was saved with.
    writer.Key("cloudsEnabled"); writer.Bool(settings.atmosphere.cloudsEnabled);
    writer.Key("cloudBaseHeight"); writer.Double(settings.atmosphere.cloudBaseHeight);
    writer.Key("cloudTopHeight"); writer.Double(settings.atmosphere.cloudTopHeight);
    writer.Key("cloudCoverage"); writer.Double(settings.atmosphere.cloudCoverage);
    writer.Key("cloudDensity"); writer.Double(settings.atmosphere.cloudDensity);
    writer.Key("cloudWindOffsetX"); writer.Double(settings.atmosphere.cloudWindOffsetX);
    writer.Key("cloudWindOffsetZ"); writer.Double(settings.atmosphere.cloudWindOffsetZ);
    writer.Key("cloudFeatureScale"); writer.Double(settings.atmosphere.cloudFeatureScale);
    writer.Key("cloudNoisePeriod"); writer.Double(settings.atmosphere.cloudNoisePeriod);
    // The weather map's controls (C5) and the march's quality tier. The tier is written as an
    // integer so the file does not depend on the enum's declaration order staying frozen: 0 is Low,
    // 1 is High, and anything else on load falls back to what the file already had.
    writer.Key("cloudWeatherScale"); writer.Double(settings.atmosphere.cloudWeatherScale);
    writer.Key("cloudCoverageVariation"); writer.Double(settings.atmosphere.cloudCoverageVariation);
    writer.Key("cloudType"); writer.Double(settings.atmosphere.cloudType);
    writer.Key("cloudTypeVariation"); writer.Double(settings.atmosphere.cloudTypeVariation);
    writer.Key("cloudHeightVariation"); writer.Double(settings.atmosphere.cloudHeightVariation);
    writer.Key("cloudDetailStrength"); writer.Double(settings.atmosphere.cloudDetailStrength);
    writer.Key("cloudDetailEdge"); writer.Double(settings.atmosphere.cloudDetailEdge);
    writer.Key("cloudHalfResolution"); writer.Bool(settings.atmosphere.cloudHalfResolution);
    writer.Key("cloudTemporalEnabled"); writer.Bool(settings.atmosphere.cloudTemporalEnabled);
    writer.Key("cloudShadowsEnabled"); writer.Bool(settings.atmosphere.cloudShadowsEnabled);
    writer.Key("cloudGodRaysEnabled"); writer.Bool(settings.atmosphere.cloudGodRaysEnabled);
    writer.Key("cloudDeterministic"); writer.Bool(settings.atmosphere.cloudDeterministic);
    writer.Key("cloudOfflineNoise"); writer.Bool(settings.atmosphere.cloudOfflineNoise);
    writer.Key("cloudGodRaysStrength"); writer.Double(settings.atmosphere.cloudGodRaysStrength);
    writer.Key("cloudQuality");
    writer.Int(settings.atmosphere.cloudQuality == atmosphere::CloudQualityTier::High ? 1 : 0);
    writer.Key("cloudHorizonFadeDegrees"); writer.Double(settings.atmosphere.cloudHorizonFadeDegrees);
    writer.Key("cloudAmbientElevationDegrees"); writer.Double(settings.atmosphere.cloudAmbientElevationDegrees);
    writer.Key("cloudAmbientScale"); writer.Double(settings.atmosphere.cloudAmbientScale);
    WRITE_FLOAT(refractionScale); WRITE_INT(refractionSteps); WRITE_FLOAT(volumeThicknessScale);
    WRITE_BOOL(geometricThicknessEnabled); WRITE_BOOL(twoInterfaceRefractionEnabled);
    WRITE_BOOL(volumeGlassOverrideEnabled); WRITE_FLOAT(volumeGlassTransmission);
    WRITE_FLOAT(volumeGlassRoughness); WRITE_VEC3(volumeGlassAttenuationColor);
    WRITE_FLOAT(volumeGlassAttenuationDistance); WRITE_BOOL(dispersionEnabled);
    WRITE_FLOAT(dispersionStrength);
    writer.Key("glassDebugView"); writer.Int(static_cast<int>(settings.glassDebugView));
    WRITE_FLOAT(exposure); WRITE_FLOAT(bloomThreshold); WRITE_FLOAT(bloomIntensity);
    WRITE_FLOAT(prismBeamOutputLength); WRITE_FLOAT(prismBeamWidth);
    WRITE_FLOAT(prismBeamIntensity); WRITE_FLOAT(prismBeamEdgeSoftness);
    WRITE_FLOAT(prismBeamBloomContribution); WRITE_FLOAT(indexOfRefractionOverride);
    WRITE_BOOL(showPrismOpticalPathDebug); WRITE_BOOL(instanceOptimizationEnabled);
    WRITE_BOOL(frustumCullingEnabled); WRITE_BOOL(lodSelectionEnabled);
    WRITE_FLOAT(lodMediumThresholdPixels); WRITE_FLOAT(lodHighThresholdPixels);
    WRITE_BOOL(ssaoEnabled); WRITE_FLOAT(ssaoRadius); WRITE_FLOAT(ssaoBias); WRITE_FLOAT(ssaoStrength);
    WRITE_BOOL(temporalAaEnabled); WRITE_FLOAT(temporalHistoryWeight);
    WRITE_INT(temporalDebugView); WRITE_INT(skinningDebugView); WRITE_BOOL(shaderHotReloadEnabled);
    writer.Key("localLights"); writer.StartArray();
    for (const LocalLight& light : settings.localLights) {
        writer.StartObject();
        writer.Key("type"); writer.Int(static_cast<int>(light.type));
        writer.Key("position"); writeVec3(writer, light.position);
        writer.Key("radius"); writer.Double(light.radius);
        writer.Key("color"); writeVec3(writer, light.color);
        writer.Key("intensity"); writer.Double(light.intensity);
        writer.Key("direction"); writeVec3(writer, light.direction);
        writer.Key("outerConeCosine"); writer.Double(light.outerConeCosine);
        writer.EndObject();
    }
    writer.EndArray();
#undef WRITE_FLOAT
#undef WRITE_INT
#undef WRITE_BOOL
#undef WRITE_VEC3
    writer.EndObject();
}

const scene_json::Value* optionalMember(
    const scene_json::Value& object,
    const char* name
) {
    if (!object.IsObject()) throw std::runtime_error("Expected a JSON object");
    const auto found = object.FindMember(name);
    return found == object.MemberEnd() ? nullptr : &found->value;
}

float readFloat(const scene_json::Value& object, const char* name, float fallback) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsNumber() || !std::isfinite(value->GetDouble())) {
        throw std::runtime_error(std::string("Expected finite number for '") + name + "'");
    }
    return static_cast<float>(value->GetDouble());
}

int readInt(const scene_json::Value& object, const char* name, int fallback) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsInt()) throw std::runtime_error(std::string("Expected integer for '") + name + "'");
    return value->GetInt();
}

bool readBool(const scene_json::Value& object, const char* name, bool fallback) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsBool()) throw std::runtime_error(std::string("Expected boolean for '") + name + "'");
    return value->GetBool();
}

std::string readString(
    const scene_json::Value& object,
    const char* name,
    std::string fallback = {}
) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsString()) throw std::runtime_error(std::string("Expected string for '") + name + "'");
    return std::string(value->GetString(), value->GetStringLength());
}

glm::vec3 readVec3(
    const scene_json::Value& object,
    const char* name,
    const glm::vec3& fallback
) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsArray() || value->Size() != 3U) {
        throw std::runtime_error(std::string("Expected three-number array for '") + name + "'");
    }
    glm::vec3 result;
    for (scene_json::SizeType index = 0; index < 3U; ++index) {
        if (!(*value)[index].IsNumber() || !std::isfinite((*value)[index].GetDouble())) {
            throw std::runtime_error(std::string("Expected finite vector for '") + name + "'");
        }
        result[static_cast<int>(index)] = static_cast<float>((*value)[index].GetDouble());
    }
    return result;
}

glm::mat4 readMatrix(
    const scene_json::Value& object,
    const char* name,
    const glm::mat4& fallback
) {
    const scene_json::Value* value = optionalMember(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsArray() || value->Size() != 16U) {
        throw std::runtime_error(std::string("Expected 16-number matrix for '") + name + "'");
    }
    glm::mat4 result(1.0f);
    for (scene_json::SizeType index = 0; index < 16U; ++index) {
        if (!(*value)[index].IsNumber() || !std::isfinite((*value)[index].GetDouble())) {
            throw std::runtime_error(std::string("Expected finite matrix for '") + name + "'");
        }
        result[static_cast<int>(index / 4U)][static_cast<int>(index % 4U)] =
            static_cast<float>((*value)[index].GetDouble());
    }
    return result;
}

void readRendererSettings(const scene_json::Value& value, RendererSettings& settings) {
#define READ_FLOAT(field) settings.field = readFloat(value, #field, settings.field)
#define READ_INT(field) settings.field = readInt(value, #field, settings.field)
#define READ_BOOL(field) settings.field = readBool(value, #field, settings.field)
#define READ_VEC3(field) settings.field = readVec3(value, #field, settings.field)
    READ_VEC3(backgroundColor); READ_VEC3(baseColor); READ_VEC3(lightDirection);
    READ_FLOAT(ambientStrength); READ_FLOAT(diffuseStrength); READ_FLOAT(specularStrength);
    READ_FLOAT(shininess); READ_INT(msaaSamples);
    settings.renderPath = static_cast<RenderPath>(readInt(value, "renderPath", static_cast<int>(settings.renderPath)));
    settings.shadingMode = static_cast<ShadingMode>(readInt(value, "shadingMode", static_cast<int>(settings.shadingMode)));
    settings.stylizedPreset = static_cast<StylizedPreset>(readInt(
        value, "stylizedPreset", static_cast<int>(settings.stylizedPreset)
    ));
    settings.gBufferDebugView = static_cast<GBufferDebugView>(readInt(value, "gBufferDebugView", static_cast<int>(settings.gBufferDebugView)));
    READ_BOOL(wireframe); READ_BOOL(cullBackFaces); READ_BOOL(normalMapping);
    READ_BOOL(showGrid); READ_BOOL(showAxes); READ_BOOL(pbrEnabled); READ_BOOL(iblEnabled);
    READ_INT(stylizedBandCount); READ_FLOAT(stylizedBandSoftness);
    READ_FLOAT(stylizedSpecularSize); READ_FLOAT(stylizedSpecularSoftness);
    READ_FLOAT(stylizedRimWidth); READ_FLOAT(stylizedRimSoftness);
    READ_FLOAT(stylizedRimIntensity); READ_VEC3(stylizedShadowTint);
    READ_VEC3(stylizedRimColor);
    READ_BOOL(stylizedOutlineEnabled); READ_FLOAT(stylizedOutlineWidth);
    READ_FLOAT(stylizedOutlineDepthThreshold); READ_FLOAT(stylizedOutlineNormalThreshold);
    READ_VEC3(stylizedOutlineColor);
    READ_BOOL(stylizedDitherEnabled); READ_FLOAT(stylizedDitherStrength);
    READ_BOOL(stylizedHeightFogEnabled); READ_FLOAT(stylizedHeightFogDensity);
    READ_FLOAT(stylizedHeightFogBaseHeight); READ_FLOAT(stylizedHeightFogFalloff);
    READ_VEC3(stylizedHeightFogColor);
    READ_BOOL(stylizedColorGradingEnabled);
    settings.stylizedColorGradingLut = static_cast<StylizedColorGradingLut>(readInt(
        value,
        "stylizedColorGradingLut",
        static_cast<int>(settings.stylizedColorGradingLut)
    ));
    READ_FLOAT(stylizedColorGradingStrength);
    settings.stylizedDebugView = static_cast<StylizedDebugView>(readInt(
        value, "stylizedDebugView", static_cast<int>(settings.stylizedDebugView)
    ));
    READ_BOOL(shadowsEnabled); READ_BOOL(coloredTransmissionShadowsEnabled);
    settings.shadowCascadeCount = std::clamp(
        readInt(value, "shadowCascadeCount", settings.shadowCascadeCount), 1, 4
    );
    settings.shadowCascadeSplitLambda = std::clamp(
        readFloat(value, "shadowCascadeSplitLambda", settings.shadowCascadeSplitLambda),
        0.0f, 1.0f
    );
    READ_BOOL(shadowCascadeDebugView);
    settings.water.enabled = readBool(value, "waterEnabled", settings.water.enabled);
    settings.water.preset = static_cast<WaterPreset>(std::clamp(
        readInt(value, "waterPreset", static_cast<int>(settings.water.preset)), 0, 3));
    settings.water.quality = static_cast<WaterQuality>(std::clamp(
        readInt(value, "waterQuality", static_cast<int>(settings.water.quality)), 0, 1));
    settings.water.level = std::clamp(readFloat(value, "waterLevel", settings.water.level), -10.0f, 10.0f);
    settings.water.extent = std::clamp(readFloat(value, "waterExtent", settings.water.extent), 20.0f, 2000.0f);
    settings.water.amplitude = std::clamp(readFloat(value, "waterAmplitude", settings.water.amplitude), 0.0f, 2.0f);
    settings.water.speed = std::clamp(readFloat(value, "waterSpeed", settings.water.speed), 0.0f, 5.0f);
    settings.water.steepness = std::clamp(readFloat(value, "waterSteepness", settings.water.steepness), 0.0f, 0.9f);
    settings.water.foamStrength = std::clamp(readFloat(value, "waterFoamStrength", settings.water.foamStrength), 0.0f, 1.0f);
    settings.water.roughness = std::clamp(readFloat(value, "waterRoughness", settings.water.roughness), 0.02f, 0.8f);
    settings.water.reflectionStrength = std::clamp(readFloat(value, "waterReflectionStrength", settings.water.reflectionStrength), 0.0f, 2.0f);
    settings.water.rippleStrength = std::clamp(readFloat(value, "waterRippleStrength", settings.water.rippleStrength), 0.0f, 1.0f);
    settings.water.sunGlintStrength = std::clamp(readFloat(value, "waterSunGlintStrength", settings.water.sunGlintStrength), 0.0f, 2.0f);
    settings.water.windDirection.x = std::clamp(
        readFloat(value, "waterWindX", settings.water.windDirection.x), -1.0f, 1.0f);
    settings.water.windDirection.y = std::clamp(
        readFloat(value, "waterWindZ", settings.water.windDirection.y), -1.0f, 1.0f);
    READ_BOOL(causticsEnabled);
    settings.causticsMode = static_cast<CausticsMode>(readInt(value, "causticsMode", static_cast<int>(settings.causticsMode)));
    READ_FLOAT(causticsStrength); READ_FLOAT(causticsScale); READ_VEC3(causticsDirection);
    READ_FLOAT(causticsSharpness); READ_BOOL(causticsAnimated); READ_FLOAT(causticsReceiverPlaneY);
    READ_BOOL(transmissionEnabled); READ_BOOL(skyboxEnabled); READ_BOOL(toneMapping); READ_BOOL(bloom);
    READ_BOOL(showPrismIncidentBeam); READ_FLOAT(environmentIntensity);
    settings.atmosphere.enabled = readBool(
        value, "atmosphereEnabled", settings.atmosphere.enabled
    );
    settings.atmosphere.sunElevationDegrees = readFloat(
        value, "sunElevationDegrees", settings.atmosphere.sunElevationDegrees
    );
    settings.atmosphere.sunAzimuthDegrees = readFloat(
        value, "sunAzimuthDegrees", settings.atmosphere.sunAzimuthDegrees
    );
    settings.atmosphere.turbidity = readFloat(
        value, "skyTurbidity", settings.atmosphere.turbidity
    );
    settings.atmosphere.skyIntensity = readFloat(
        value, "skyIntensity", settings.atmosphere.skyIntensity
    );
    settings.atmosphere.sunIntensity = readFloat(
        value, "sunIntensity", settings.atmosphere.sunIntensity
    );
    settings.atmosphere.groundAlbedo = readFloat(
        value, "groundAlbedo", settings.atmosphere.groundAlbedo
    );
    settings.atmosphere.nightSkyEnabled = readBool(
        value, "nightSkyEnabled", settings.atmosphere.nightSkyEnabled
    );
    settings.atmosphere.moonIntensity = std::clamp(readFloat(
        value, "moonIntensity", settings.atmosphere.moonIntensity), 0.0f, 4.0f);
    settings.atmosphere.starIntensity = std::clamp(readFloat(
        value, "starIntensity", settings.atmosphere.starIntensity), 0.0f, 4.0f);
    settings.atmosphere.aerialPerspectiveEnabled = readBool(
        value, "aerialPerspectiveEnabled", settings.atmosphere.aerialPerspectiveEnabled
    );
    settings.atmosphere.aerialPerspectiveStrength = readFloat(
        value, "aerialPerspectiveStrength", settings.atmosphere.aerialPerspectiveStrength
    );
    settings.atmosphere.aerialPerspectiveScaleHeight = readFloat(
        value, "aerialPerspectiveScaleHeight", settings.atmosphere.aerialPerspectiveScaleHeight
    );
    // Cloud fields default to the pre-slice values, so a scene written before the layer existed
    // loads as a cloudless sky instead of as a scene full of clouds.
    settings.atmosphere.cloudsEnabled = readBool(
        value, "cloudsEnabled", settings.atmosphere.cloudsEnabled
    );
    settings.atmosphere.cloudBaseHeight = readFloat(
        value, "cloudBaseHeight", settings.atmosphere.cloudBaseHeight
    );
    settings.atmosphere.cloudTopHeight = readFloat(
        value, "cloudTopHeight", settings.atmosphere.cloudTopHeight
    );
    settings.atmosphere.cloudCoverage = std::clamp(readFloat(
        value, "cloudCoverage", settings.atmosphere.cloudCoverage), 0.0f, 1.0f);
    settings.atmosphere.cloudDensity = std::clamp(readFloat(
        value, "cloudDensity", settings.atmosphere.cloudDensity), 0.0f, 4.0f);
    settings.atmosphere.cloudWindOffsetX = readFloat(
        value, "cloudWindOffsetX", settings.atmosphere.cloudWindOffsetX
    );
    settings.atmosphere.cloudWindOffsetZ = readFloat(
        value, "cloudWindOffsetZ", settings.atmosphere.cloudWindOffsetZ
    );
    settings.atmosphere.cloudFeatureScale = readFloat(
        value, "cloudFeatureScale", settings.atmosphere.cloudFeatureScale
    );
    settings.atmosphere.cloudNoisePeriod = std::clamp(readFloat(
        value, "cloudNoisePeriod", settings.atmosphere.cloudNoisePeriod), 1.0f, 16.0f);
    // A scene written before C5 has none of these keys, so each falls back to the value the struct
    // already carries. That is the backwards-compatibility contract: an old scene loads with the
    // weather map inert at whatever the defaults are rather than failing to open.
    settings.atmosphere.cloudWeatherScale = std::clamp(readFloat(
        value, "cloudWeatherScale", settings.atmosphere.cloudWeatherScale), 100.0f, 400000.0f);
    settings.atmosphere.cloudCoverageVariation = std::clamp(readFloat(
        value, "cloudCoverageVariation", settings.atmosphere.cloudCoverageVariation), 0.0f, 1.0f);
    settings.atmosphere.cloudType = std::clamp(readFloat(
        value, "cloudType", settings.atmosphere.cloudType), 0.0f, 1.0f);
    settings.atmosphere.cloudTypeVariation = std::clamp(readFloat(
        value, "cloudTypeVariation", settings.atmosphere.cloudTypeVariation), 0.0f, 1.0f);
    settings.atmosphere.cloudHeightVariation = std::clamp(readFloat(
        value, "cloudHeightVariation", settings.atmosphere.cloudHeightVariation), 0.0f, 1.0f);
    settings.atmosphere.cloudDetailStrength = std::clamp(readFloat(
        value, "cloudDetailStrength", settings.atmosphere.cloudDetailStrength), 0.0f, 1.0f);
    settings.atmosphere.cloudHalfResolution = readBool(value, "cloudHalfResolution", false);
    settings.atmosphere.cloudTemporalEnabled = readBool(value, "cloudTemporalEnabled", false);
    settings.atmosphere.cloudShadowsEnabled = readBool(value, "cloudShadowsEnabled", false);
    settings.atmosphere.cloudGodRaysEnabled = readBool(value, "cloudGodRaysEnabled", false);
    settings.atmosphere.cloudDeterministic = readBool(value, "cloudDeterministic", false);
    settings.atmosphere.cloudOfflineNoise = readBool(value,"cloudOfflineNoise",false);
    if(settings.atmosphere.cloudOfflineNoise && std::lround(settings.atmosphere.cloudNoisePeriod)!=4)
        throw std::runtime_error("Offline cloud noise requires period 4");
    if(settings.atmosphere.cloudOfflineNoise) {
        (void)cloud::canonicalNoiseVolume();
        (void)cloud::canonicalLightingLut();
    }
    settings.atmosphere.cloudGodRaysStrength = std::clamp(readFloat(value, "cloudGodRaysStrength", 0.08f), 0.0f, 1.0f);
    settings.atmosphere.cloudDetailEdge = std::clamp(readFloat(
        value, "cloudDetailEdge", settings.atmosphere.cloudDetailEdge), 0.0f, 1.0f);
    {
        const int quality = readInt(value, "cloudQuality",
            settings.atmosphere.cloudQuality == atmosphere::CloudQualityTier::High ? 1 : 0);
        settings.atmosphere.cloudQuality = quality == 1
            ? atmosphere::CloudQualityTier::High : atmosphere::CloudQualityTier::Low;
    }
    settings.atmosphere.cloudHorizonFadeDegrees = std::clamp(readFloat(
        value, "cloudHorizonFadeDegrees", settings.atmosphere.cloudHorizonFadeDegrees), 0.0f, 30.0f);
    settings.atmosphere.cloudAmbientElevationDegrees = std::clamp(readFloat(
        value, "cloudAmbientElevationDegrees",
        settings.atmosphere.cloudAmbientElevationDegrees), 0.0f, 89.0f);
    settings.atmosphere.cloudAmbientScale = std::clamp(readFloat(
        value, "cloudAmbientScale", settings.atmosphere.cloudAmbientScale), 0.0f, 4.0f);
    READ_FLOAT(refractionScale);
    READ_INT(refractionSteps); READ_FLOAT(volumeThicknessScale); READ_BOOL(geometricThicknessEnabled);
    READ_BOOL(twoInterfaceRefractionEnabled); READ_BOOL(volumeGlassOverrideEnabled);
    READ_FLOAT(volumeGlassTransmission); READ_FLOAT(volumeGlassRoughness);
    READ_VEC3(volumeGlassAttenuationColor); READ_FLOAT(volumeGlassAttenuationDistance);
    READ_BOOL(dispersionEnabled); READ_FLOAT(dispersionStrength);
    settings.glassDebugView = static_cast<GlassDebugView>(readInt(value, "glassDebugView", static_cast<int>(settings.glassDebugView)));
    READ_FLOAT(exposure); READ_FLOAT(bloomThreshold); READ_FLOAT(bloomIntensity);
    READ_FLOAT(prismBeamOutputLength); READ_FLOAT(prismBeamWidth); READ_FLOAT(prismBeamIntensity);
    READ_FLOAT(prismBeamEdgeSoftness); READ_FLOAT(prismBeamBloomContribution);
    READ_FLOAT(indexOfRefractionOverride); READ_BOOL(showPrismOpticalPathDebug);
    READ_BOOL(instanceOptimizationEnabled); READ_BOOL(frustumCullingEnabled);
    READ_BOOL(lodSelectionEnabled); READ_FLOAT(lodMediumThresholdPixels);
    READ_FLOAT(lodHighThresholdPixels); READ_BOOL(ssaoEnabled); READ_FLOAT(ssaoRadius);
    READ_FLOAT(ssaoBias); READ_FLOAT(ssaoStrength); READ_BOOL(temporalAaEnabled);
    READ_FLOAT(temporalHistoryWeight); READ_INT(temporalDebugView); READ_INT(skinningDebugView);
    READ_BOOL(shaderHotReloadEnabled);
    if (const scene_json::Value* lights = optionalMember(value, "localLights")) {
        if (!lights->IsArray()) throw std::runtime_error("Expected array for 'localLights'");
        settings.localLights.clear();
        settings.localLights.reserve(lights->Size());
        for (const scene_json::Value& item : lights->GetArray()) {
            if (!item.IsObject()) throw std::runtime_error("Expected local light object");
            LocalLight light;
            light.type = static_cast<LocalLightType>(readInt(item, "type", static_cast<int>(light.type)));
            light.position = readVec3(item, "position", light.position);
            light.radius = readFloat(item, "radius", light.radius);
            light.color = readVec3(item, "color", light.color);
            light.intensity = readFloat(item, "intensity", light.intensity);
            light.direction = readVec3(item, "direction", light.direction);
            light.outerConeCosine = readFloat(item, "outerConeCosine", light.outerConeCosine);
            settings.localLights.push_back(light);
        }
    }
#undef READ_FLOAT
#undef READ_INT
#undef READ_BOOL
#undef READ_VEC3
}

void validateDocument(const SceneDocument& document) {
    std::unordered_map<SceneEntityId, SceneEntityId> parents;
    for (const SceneDocumentEntity& entity : document.entities) {
        if (entity.id == invalidSceneEntityId || !parents.emplace(entity.id, entity.parent).second) {
            throw std::runtime_error("Scene entity IDs must be unique non-zero values");
        }
    }
    for (const auto& pair : parents) {
        if (pair.second != invalidSceneEntityId && parents.count(pair.second) == 0U) {
            throw std::runtime_error("Scene entity references a missing parent");
        }
        std::unordered_set<SceneEntityId> visited;
        SceneEntityId current = pair.first;
        while (current != invalidSceneEntityId) {
            if (!visited.insert(current).second) throw std::runtime_error("Scene entity hierarchy contains a cycle");
            current = parents.at(current);
        }
    }
}

} // namespace

bool saveSceneDocument(
    const std::filesystem::path& path,
    const SceneDocument& document,
    std::string& error
) {
    error.clear();
    try {
        validateDocument(document);
        scene_json::StringBuffer buffer;
        Writer writer(buffer);
        writer.SetIndent(' ', 2);
        writer.StartObject();
        writer.Key("format"); writer.String("MyRendererScene");
        writer.Key("version"); writer.Int(SceneDocument::currentVersion);
        writer.Key("camera"); writer.StartObject();
        writer.Key("target"); writeVec3(writer, document.camera.target);
        writer.Key("yawDegrees"); writer.Double(document.camera.yawDegrees);
        writer.Key("pitchDegrees"); writer.Double(document.camera.pitchDegrees);
        writer.Key("distance"); writer.Double(document.camera.distance);
        writer.Key("fieldOfViewDegrees"); writer.Double(document.camera.fieldOfViewDegrees);
        if (document.camera.farPlane != 100.0f) {
            writer.Key("farPlane"); writer.Double(document.camera.farPlane);
        }
        writer.EndObject();
        writer.Key("renderer"); writeRendererSettings(writer, document.renderer);
        writer.Key("playback"); writer.StartObject();
        writer.Key("animationEnabled"); writer.Bool(document.playback.animationEnabled);
        writer.Key("animationPlaying"); writer.Bool(document.playback.animationPlaying);
        writer.Key("animationTimeSeconds"); writer.Double(document.playback.animationTimeSeconds);
        writer.Key("animationSpeed"); writer.Double(document.playback.animationSpeed);
        writer.Key("animationClipIndex"); writer.Uint64(document.playback.animationClipIndex);
        writer.Key("prismEnabled"); writer.Bool(document.playback.prismEnabled);
        writer.Key("prismCameraLocked"); writer.Bool(document.playback.prismCameraLocked);
        writer.Key("prismPreset"); writer.Int(static_cast<int>(document.playback.prismPreset));
        writer.Key("prismParameters"); writer.StartObject();
        writer.Key("beamAngleDegrees"); writer.Double(document.playback.prismParameters.beamAngleDegrees);
        writer.Key("centralIndexOfRefraction"); writer.Double(document.playback.prismParameters.centralIndexOfRefraction);
        writer.Key("dispersion"); writer.Double(document.playback.prismParameters.dispersion);
        writer.Key("spectralSampleCount"); writer.Int(document.playback.prismParameters.spectralSampleCount);
        writer.Key("spectrumMode"); writer.Int(static_cast<int>(document.playback.prismParameters.spectrumMode));
        writer.Key("whitePointKelvin"); writer.Double(document.playback.prismParameters.whitePointKelvin);
        writer.Key("attenuationDistance"); writer.Double(document.playback.prismParameters.attenuationDistance);
        writer.Key("attenuationColor"); writeVec3(writer, document.playback.prismParameters.attenuationColor);
        writer.EndObject();
        writer.EndObject();
        writer.Key("entities"); writer.StartArray();
        for (const SceneDocumentEntity& entity : document.entities) {
            writer.StartObject();
            writer.Key("id"); writer.Uint64(entity.id);
            writer.Key("name"); writer.String(entity.name.c_str(), static_cast<scene_json::SizeType>(entity.name.size()));
            writer.Key("parent"); writer.Uint64(entity.parent);
            writer.Key("model");
            const std::string relative = makeSceneRelativeResource(entity.modelResource, path);
            writer.String(relative.c_str(), static_cast<scene_json::SizeType>(relative.size()));
            writer.Key("transform"); writer.StartObject();
            writer.Key("translation"); writeVec3(writer, entity.transform.translation);
            writer.Key("rotationDegrees"); writeVec3(writer, entity.transform.rotationDegrees);
            writer.Key("scale"); writeVec3(writer, entity.transform.scale);
            writer.Key("assetMatrix"); writeMatrix(writer, entity.transform.assetTransform);
            writer.EndObject();
            writer.Key("visible"); writer.Bool(entity.visible);
            writer.Key("tint"); writeVec3(writer, entity.tint);
            writer.Key("castsShadow"); writer.Bool(entity.castsShadow);
            writer.Key("instanceCandidate"); writer.Bool(entity.instanceCandidate);
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();

        std::error_code filesystemError;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), filesystemError);
        if (filesystemError) throw std::runtime_error("Cannot create scene directory: " + filesystemError.message());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("Cannot open scene file for writing");
        stream.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
        if (!stream) throw std::runtime_error("Failed while writing scene file");
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool loadSceneDocument(
    const std::filesystem::path& path,
    SceneDocument& document,
    std::string& error
) {
    error.clear();
    try {
        capture::recordInput(path);
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open scene file");
        const std::string json(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>()
        );
        scene_json::Document root;
        root.Parse(json.data(), json.size());
        if (root.HasParseError()) {
            throw std::runtime_error(
                std::string("JSON parse error: ") + scene_json::GetParseError_En(root.GetParseError())
            );
        }
        if (!root.IsObject() || readString(root, "format") != "MyRendererScene") {
            throw std::runtime_error("Not a MyRenderer scene file");
        }
        const int version = readInt(root, "version", 0);
        if (version < 1 || version > SceneDocument::currentVersion) {
            throw std::runtime_error("Unsupported scene version " + std::to_string(version));
        }

        SceneDocument loaded;
        if (const scene_json::Value* camera = optionalMember(root, "camera")) {
            loaded.camera.target = readVec3(*camera, "target", loaded.camera.target);
            loaded.camera.yawDegrees = readFloat(*camera, "yawDegrees", loaded.camera.yawDegrees);
            loaded.camera.pitchDegrees = readFloat(*camera, "pitchDegrees", loaded.camera.pitchDegrees);
            loaded.camera.distance = readFloat(*camera, "distance", loaded.camera.distance);
            loaded.camera.fieldOfViewDegrees = readFloat(*camera, "fieldOfViewDegrees", loaded.camera.fieldOfViewDegrees);
            loaded.camera.farPlane = std::clamp(
                readFloat(*camera, "farPlane", loaded.camera.farPlane), 100.0f, 2000.0f);
        }
        if (const scene_json::Value* renderer = optionalMember(root, "renderer")) {
            readRendererSettings(*renderer, loaded.renderer);
        }
        if (const scene_json::Value* playback = optionalMember(root, "playback")) {
            loaded.playback.animationEnabled = readBool(*playback, "animationEnabled", loaded.playback.animationEnabled);
            loaded.playback.animationPlaying = readBool(*playback, "animationPlaying", loaded.playback.animationPlaying);
            loaded.playback.animationTimeSeconds = readFloat(*playback, "animationTimeSeconds", loaded.playback.animationTimeSeconds);
            loaded.playback.animationSpeed = readFloat(*playback, "animationSpeed", loaded.playback.animationSpeed);
            if (const scene_json::Value* clip = optionalMember(*playback, "animationClipIndex")) {
                if (!clip->IsUint64()) throw std::runtime_error("Expected unsigned animationClipIndex");
                loaded.playback.animationClipIndex = static_cast<std::size_t>(clip->GetUint64());
            }
            loaded.playback.prismEnabled = readBool(*playback, "prismEnabled", loaded.playback.prismEnabled);
            loaded.playback.prismCameraLocked = readBool(*playback, "prismCameraLocked", loaded.playback.prismCameraLocked);
            loaded.playback.prismPreset = static_cast<PrismOpticalPreset>(readInt(*playback, "prismPreset", static_cast<int>(loaded.playback.prismPreset)));
            if (const scene_json::Value* prism = optionalMember(*playback, "prismParameters")) {
                loaded.playback.prismParameters.beamAngleDegrees = readFloat(*prism, "beamAngleDegrees", loaded.playback.prismParameters.beamAngleDegrees);
                loaded.playback.prismParameters.centralIndexOfRefraction = readFloat(*prism, "centralIndexOfRefraction", loaded.playback.prismParameters.centralIndexOfRefraction);
                loaded.playback.prismParameters.dispersion = readFloat(*prism, "dispersion", loaded.playback.prismParameters.dispersion);
                loaded.playback.prismParameters.spectralSampleCount = readInt(*prism, "spectralSampleCount", loaded.playback.prismParameters.spectralSampleCount);
                loaded.playback.prismParameters.spectrumMode = static_cast<PrismSpectrumMode>(readInt(*prism, "spectrumMode", static_cast<int>(loaded.playback.prismParameters.spectrumMode)));
                loaded.playback.prismParameters.whitePointKelvin = readFloat(*prism, "whitePointKelvin", loaded.playback.prismParameters.whitePointKelvin);
                loaded.playback.prismParameters.attenuationDistance = readFloat(*prism, "attenuationDistance", loaded.playback.prismParameters.attenuationDistance);
                loaded.playback.prismParameters.attenuationColor = readVec3(*prism, "attenuationColor", loaded.playback.prismParameters.attenuationColor);
            }
        }
        const scene_json::Value* entities = optionalMember(root, "entities");
        if (entities == nullptr || !entities->IsArray()) throw std::runtime_error("Scene requires an entities array");
        loaded.entities.reserve(entities->Size());
        for (const scene_json::Value& item : entities->GetArray()) {
            if (!item.IsObject()) throw std::runtime_error("Expected scene entity object");
            const scene_json::Value* id = optionalMember(item, "id");
            const scene_json::Value* parent = optionalMember(item, "parent");
            if (id == nullptr || !id->IsUint64()) throw std::runtime_error("Entity requires unsigned id");
            if (parent != nullptr && !parent->IsUint64()) throw std::runtime_error("Entity parent must be unsigned");
            SceneDocumentEntity entity;
            entity.id = id->GetUint64();
            entity.parent = parent == nullptr ? invalidSceneEntityId : parent->GetUint64();
            entity.name = readString(item, "name", "Entity");
            entity.modelResource = readString(item, "model");
            if (const scene_json::Value* transform = optionalMember(item, "transform")) {
                entity.transform.translation = readVec3(*transform, "translation", entity.transform.translation);
                entity.transform.rotationDegrees = readVec3(*transform, "rotationDegrees", entity.transform.rotationDegrees);
                entity.transform.scale = readVec3(*transform, "scale", entity.transform.scale);
                entity.transform.assetTransform = readMatrix(*transform, "assetMatrix", entity.transform.assetTransform);
            }
            entity.visible = readBool(item, "visible", entity.visible);
            entity.tint = readVec3(item, "tint", entity.tint);
            entity.castsShadow = readBool(item, "castsShadow", entity.castsShadow);
            entity.instanceCandidate = readBool(item, "instanceCandidate", entity.instanceCandidate);
            loaded.entities.push_back(std::move(entity));
        }
        validateDocument(loaded);
        document = std::move(loaded);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

std::string makeSceneRelativeResource(
    const std::string& resource,
    const std::filesystem::path& scenePath
) {
    if (resource.empty() || resource.rfind("builtin:", 0U) == 0U) return resource;
    const std::filesystem::path value = std::filesystem::u8path(resource);
    if (!value.is_absolute()) return value.generic_u8string();
    const std::filesystem::path relative = value.lexically_normal().lexically_relative(
        scenePath.parent_path().lexically_normal()
    );
    return relative.empty() ? value.generic_u8string() : relative.generic_u8string();
}

std::filesystem::path resolveSceneResource(
    const std::string& resource,
    const std::filesystem::path& scenePath
) {
    if (resource.empty() || resource.rfind("builtin:", 0U) == 0U) return {};
    const std::filesystem::path value = std::filesystem::u8path(resource);
    if (value.is_absolute()) return value.lexically_normal();
    return std::filesystem::absolute(scenePath.parent_path() / value).lexically_normal();
}
