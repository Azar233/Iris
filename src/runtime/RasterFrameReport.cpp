#include "runtime/RasterFrameReport.h"
#include "optics/CloudNoiseVolume.h"
#include "optics/CloudLightingLut.h"
#include <fstream>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#define RAPIDJSON_NAMESPACE myrenderer_raster_report_json
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>
#undef RAPIDJSON_NAMESPACE
#undef RAPIDJSON_NAMESPACE_BEGIN
#undef RAPIDJSON_NAMESPACE_END
namespace json = myrenderer_raster_report_json;
namespace {
std::string fileFingerprint(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error("Cannot fingerprint: " + path.string());
    // FNV-1a is a content fingerprint, not a security or cross-driver pixel guarantee.
    std::uint64_t hash=14695981039346656037ULL;
    char data[4096];
    while(file){file.read(data,sizeof(data));for(std::streamsize i=0;i<file.gcount();++i){
        hash^=static_cast<unsigned char>(data[i]);hash*=1099511628211ULL;
    }}
    if(!file.eof()) throw std::runtime_error("Cannot read fingerprint input: " + path.string());
    std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return out.str();
}
}
bool writeRasterFrameReport(const std::filesystem::path& path,const RenderJob& job,
    int frame,const RendererSettings& settings,const std::string& gpu,std::string& error,const capture::InputManifest* manifest) {
    try {
        if(manifest) manifest->validate();
        json::StringBuffer buffer;json::PrettyWriter<json::StringBuffer> w(buffer);
        w.StartObject();
        w.Key("format");w.String("MyRendererRasterFrameReport");
        w.Key("schemaVersion");w.Int(1);
        w.Key("jobSchemaVersion");w.Int(job.schemaVersion);
        w.Key("frame");w.Int(frame);
        w.Key("timeSeconds");w.Double(static_cast<double>(frame-job.startFrame)/job.framesPerSecond);
        w.Key("resolution");w.StartArray();w.Uint(job.renderSettings.width);w.Uint(job.renderSettings.height);w.EndArray();
        w.Key("gpu");w.String(gpu.c_str());
        w.Key("sceneFingerprint");w.String(fileFingerprint(job.scenePath).c_str());
        w.Key("jobFingerprint");w.String(job.sourcePath.empty()?"":fileFingerprint(job.sourcePath).c_str()); // Authored job content.
        w.Key("fingerprintAlgorithm");w.String("fnv1a64");
        w.Key("inputManifest");w.StartObject();
        w.Key("version");w.Int(1);w.Key("complete");w.Bool(manifest && manifest->runtimeBinariesCaptured);
        w.Key("policy");w.String("capture-before-consumption-validate-before-publication");
        w.Key("fingerprintAlgorithm");w.String("fnv1a64");
        w.Key("files");w.StartArray();
        if(manifest)for(const auto& pair:manifest->files){const auto& input=pair.second;
            w.StartObject();w.Key("path");w.String(input.path.generic_string().c_str());
            w.Key("exists");w.Bool(input.exists);w.Key("bytes");w.Uint64(input.bytes);
            w.Key("fingerprint");w.String(input.fingerprint.c_str());w.EndObject();}
        w.EndArray();w.EndObject();
        w.Key("module");w.String(job.module.id.c_str());
        w.Key("moduleSeed");w.Uint(job.module.seed);
        w.Key("samplingSeed");w.Uint(job.renderSettings.seed);
        w.Key("capture");w.StartObject();
        w.Key("determinism");w.Bool(job.rasterDeterminism);
        w.Key("warmupFrames");w.Int(job.rasterWarmupFrames);
        w.Key("renderedFramesAtFixedTime");w.Int(job.rasterWarmupFrames+1);
        w.Key("temporalAccumulation");w.Bool(job.rasterTemporalAccumulation);
        w.Key("historyResetPerOutput");w.Bool(true);
        w.Key("effectiveTaa");w.Bool(settings.enscapeCubeShaderEnabled || (!job.rasterDeterminism && settings.temporalAaEnabled));
        w.Key("effectiveCloudHistory");w.Bool(!settings.enscapeCubeShaderEnabled && !job.rasterDeterminism && settings.atmosphere.enabled
            && settings.atmosphere.cloudsEnabled && settings.atmosphere.cloudTemporalEnabled);
        w.Key("cloudJitterSequence");w.String(job.rasterTemporalAccumulation ? "frame-index-within-output" : "fixed-spatial");
        w.Key("adaptiveCloudSteps");w.Bool(false);
        w.EndObject();
        const auto& p=settings.atmosphere;
        w.Key("cloud");w.StartObject();
        w.Key("enabled");w.Bool(!settings.enscapeCubeShaderEnabled && p.enabled && p.cloudsEnabled);
        w.Key("densitySource");w.String(p.cloudOfflineNoise ? "offline-rgba16-shared-field" : "procedural-shared-field");
        w.Key("lightingTransportSource");w.String(p.cloudOfflineNoise?"offline-beer-lambert-lut":"analytic-exp");
        w.Key("weatherSource");w.String("procedural-shared-weather");
        w.Key("offlineAssets");w.StartArray();
        if(p.cloudOfflineNoise) {
            if(std::lround(p.cloudNoisePeriod)!=4) throw std::runtime_error("Offline cloud noise requires period 4");
            const auto& volume=cloud::canonicalNoiseVolume();
            w.StartObject();
            w.Key("resource");w.String(cloud::canonicalNoiseResource);
            w.Key("fingerprint");w.String(volume.fingerprint().c_str());
            w.Key("fingerprintAlgorithm");w.String("fnv1a64");
            w.Key("generatorVersion");w.Int(1);
            w.Key("resolution");w.Int(volume.resolution);w.Key("period");w.Int(volume.period);
            w.Key("encoding");w.String("rgba16-unorm-little-endian");
            w.EndObject();
            const auto& lut=cloud::canonicalLightingLut();
            w.StartObject();w.Key("resource");w.String(cloud::canonicalLightingResource);
            w.Key("fingerprint");w.String(lut.fingerprint().c_str());w.Key("fingerprintAlgorithm");w.String("fnv1a64");
            w.Key("generatorVersion");w.Int(1);w.Key("samples");w.Int(cloud::LightingLut::count);
            w.Key("encoding");w.String("q24-little-endian");w.EndObject();
        }
        w.EndArray();
        w.Key("quality");w.String(p.cloudQuality==atmosphere::CloudQualityTier::High?"high":"low");
        w.Key("halfResolution");w.Bool(p.cloudHalfResolution);
        w.Key("heightLighting");w.Bool(p.cloudHeightLighting);
        w.Key("shapeBlend");w.Double(p.cloudShapeBlend);
        w.Key("baseHeight");w.Double(p.cloudBaseHeight);w.Key("topHeight");w.Double(p.cloudTopHeight);
        w.Key("featureScale");w.Double(p.cloudFeatureScale);w.Key("noisePeriod");w.Double(p.cloudNoisePeriod);
        w.Key("coverage");w.Double(p.cloudCoverage);w.Key("density");w.Double(p.cloudDensity);
        w.Key("wind");w.StartArray();w.Double(p.cloudWindOffsetX);w.Double(p.cloudWindOffsetZ);w.EndArray();
        w.Key("sun");w.StartArray();w.Double(p.sunElevationDegrees);w.Double(p.sunAzimuthDegrees);w.EndArray();
        w.EndObject();
        w.Key("glslOcean");w.StartObject();
        w.Key("enabled");w.Bool(settings.enscapeCubeShaderEnabled);
        w.Key("cubeEnabled");w.Bool(settings.enscapeCube.cubeEnabled);
        w.Key("noiseReduction");w.Bool(settings.enscapeCube.noiseReduction);
        w.Key("waveHeight");w.Double(settings.enscapeCube.waveHeight);
        w.Key("waveFrequency");w.Double(settings.enscapeCube.waveFrequency);
        w.Key("reflectionStrength");w.Double(settings.enscapeCube.reflectionStrength);
        w.Key("exposure");w.Double(settings.enscapeCube.exposure);
        w.EndObject();
        w.Key("water");w.StartObject();
        w.Key("enabled");w.Bool(!settings.enscapeCubeShaderEnabled && settings.water.enabled);
        w.Key("surfaceOptics");w.Bool(settings.water.surfaceOptics);
        w.Key("cloudReflectionStrength");w.Double(settings.water.cloudReflectionStrength);
        w.Key("amplitude");w.Double(settings.water.amplitude);
        w.Key("roughness");w.Double(settings.water.roughness);
        w.Key("rippleStrength");w.Double(settings.water.rippleStrength);
        w.EndObject();w.EndObject();
        std::ofstream file(path,std::ios::binary);file.write(buffer.GetString(),static_cast<std::streamsize>(buffer.GetSize()));
        file.flush();if(!file) throw std::runtime_error("Cannot write raster frame report: " + path.string());
        error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
