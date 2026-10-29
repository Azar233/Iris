#include "runtime/RasterFrameReport.h"
#include "runtime/ShaderSceneSequence.h"
#include "runtime/ModuleRunReportJson.h"
#include "plugin/ShaderScene.h"
#include "plugin/RenderPlugin.h"
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
    int frame,const RendererSettings& settings,const std::string& gpu,std::string& error,const capture::InputManifest* manifest,
    const iris::ShaderScenePackage* package, const SceneDocument* scene, const ModuleRunReport* moduleReport,
    const iris::vulkan::RasterStatistics* vulkan) {
    try {
        if(manifest) manifest->validate();
        json::StringBuffer buffer;json::PrettyWriter<json::StringBuffer> w(buffer);
        w.StartObject();
        w.Key("format");w.String("MyRendererRasterFrameReport");
        w.Key("schemaVersion");w.Int(1);
        w.Key("jobSchemaVersion");w.Int(job.schemaVersion);
        w.Key("backend");w.String(job.rasterBackend.c_str());
        if(job.rasterBackend=="vulkan") {
            if(!vulkan)throw std::runtime_error("Vulkan frame report requires actual GPU statistics");
            w.Key("vulkan");w.StartObject();
            w.Key("device");w.String(vulkan->device.c_str());
            w.Key("cpuMilliseconds");w.Double(vulkan->cpuMilliseconds);
            w.Key("gpuMilliseconds");w.Double(vulkan->gpuMilliseconds);
            w.Key("allocatedBytes");w.Uint64(vulkan->allocatedBytes);
            w.Key("deviceLocalBytes");w.Uint64(vulkan->deviceLocalBytes);
            w.Key("swapchainPayloadEstimateBytes");w.Uint64(vulkan->swapchainPayloadEstimateBytes);
            w.Key("triangles");w.Uint(vulkan->triangles);w.Key("draws");w.Uint(vulkan->draws);
            w.Key("validationEnabled");w.Bool(vulkan->validationEnabled);
            w.Key("synchronizationValidation");w.Bool(vulkan->synchronizationValidation);
            w.Key("validationWarnings");w.Uint(vulkan->validationWarnings);w.Key("validationErrors");w.Uint(vulkan->validationErrors);
            w.Key("presentation");w.String("synchronous-readback-to-host-texture");
            w.Key("shadows");w.Bool(vulkan->rayQueryShadows);w.Key("temporalAccumulation");w.Bool(false);
            w.Key("rayQueryAvailable");w.Bool(vulkan->rayQueryAvailable);w.Key("shadowDebug");w.Bool(vulkan->shadowDebug);
            w.Key("shadowMethod");w.String(vulkan->rayQueryShadows?"ray-query-directional-hard":"none");
            w.Key("blasCount");w.Uint(vulkan->blasCount);w.Key("instanceCount");w.Uint(vulkan->instanceCount);
            w.Key("blasBuilds");w.Uint(vulkan->blasBuilds);w.Key("tlasBuilds");w.Uint(vulkan->tlasBuilds);w.Key("tlasUpdates");w.Uint(vulkan->tlasUpdates);
            w.Key("accelerationBytes");w.Uint64(vulkan->accelerationBytes);
            w.Key("accelerationCpuMilliseconds");w.Double(vulkan->accelerationCpuMilliseconds);
            w.Key("accelerationGpuMilliseconds");w.Double(vulkan->accelerationGpuMilliseconds);w.EndObject();
        }
        w.Key("frame");w.Int(frame);
        w.Key("timeSeconds");w.Double(static_cast<double>(job.shaderSceneSequence ? frame : frame-job.startFrame)/job.framesPerSecond);
        w.Key("fps");w.Int(job.framesPerSecond);
        w.Key("timeStepSeconds");w.Double(1.0/job.framesPerSecond);
        w.Key("buildId");w.String(MYRENDERER_BUILD_ID);
        w.Key("renderPluginApiVersion");w.Int(iris::renderPluginApiVersion);
        if(job.shaderSceneSequence) {
            if(!package) throw std::runtime_error("ShaderScene report requires the committed package");
            if(settings.shaderSceneMouse != glm::vec4(0)) throw std::runtime_error("ShaderScene output requires zero mouse input");
            w.Key("shaderScene");w.StartObject();
            w.Key("version");w.Int(1);w.Key("packageId");w.String(package->id.c_str());
            w.Key("packageVersion");w.Int(package->version);
            w.Key("author");w.String(package->author.c_str());
            w.Key("license");w.String(package->license.c_str());
            w.Key("parameterSource");w.String(package->parameterSource.c_str());
            w.Key("manifest");w.String(package->manifest.generic_u8string().c_str());
            w.Key("entry");w.String(package->mainImageEntry?"mainImage":"main");
            w.Key("renderPluginApiVersion");w.Int(iris::renderPluginApiVersion);
            w.Key("buildId");w.String(MYRENDERER_BUILD_ID);
            w.Key("history");w.String("reset-at-start");
            w.Key("firstSampleFrame");w.Int(job.startFrame-job.shaderSceneWarmupFrames);
            w.Key("sampleIndex");w.Int(frame-job.startFrame+job.shaderSceneWarmupFrames);
            w.Key("warmupFrames");w.Int(job.shaderSceneWarmupFrames);
            w.Key("seed");w.Int(settings.shaderSceneSeed);
            w.Key("seedUniform");w.String("irisSeed");
            w.Key("mouse");w.StartObject();w.Key("version");w.Int(1);
            w.Key("uniform");w.String("irisMouse");w.Key("policy");w.String("zero-input");
            w.Key("value");w.StartArray();for(int i=0;i<4;++i)w.Double(settings.shaderSceneMouse[i]);w.EndArray();w.EndObject();
            w.Key("effectiveTimeSeconds");w.Double(settings.water.timeSeconds);
            w.Key("historyPolicy");w.StartObject();
            w.Key("resetOnParameters");w.Bool(settings.shaderSceneHistory.resetOnParameters);
            w.Key("resetOnCamera");w.Bool(settings.shaderSceneHistory.resetOnCamera);
            w.Key("maximumTimeStep");w.Double(settings.shaderSceneHistory.maximumTimeStep);w.EndObject();
            w.Key("parameters");w.StartObject();
            const auto values=iris::shaderSceneParameterValues(*package,settings);
            const auto& descriptors=package->parameters.descriptors();
            for(std::size_t i=0;i<values.size();++i){w.Key(descriptors[i].id.c_str());
                if(values[i].type==ModuleParameterType::Bool)w.Bool(values[i].boolean);
                else if(values[i].type==ModuleParameterType::Int)w.Int(values[i].integer);
                else w.Double(values[i].number);}
            w.EndObject();
            w.Key("passes");w.StartArray();for(const auto index:package->executionOrder){const auto& pass=package->passes[index];
                w.StartObject();w.Key("id");w.String(pass.id.c_str());w.Key("fragment");w.String(pass.fragment.generic_u8string().c_str());
                w.Key("format");w.String(pass.format==iris::ShaderSceneFormat::LinearRgba16f?"linear-rgba16f":"display-rgba8");
                w.Key("sizeDivisor");w.Int(pass.sizeDivisor);
                w.Key("channels");w.StartArray();for(const auto& channel:pass.channels){
                    w.StartObject();w.Key("slot");w.Int(channel.slot);w.Key("source");w.String(channel.source.c_str());
                    w.Key("previousFrame");w.Bool(channel.previousFrame);w.Key("externalTexture");w.Bool(channel.externalTexture);
                    w.Key("linearFilter");w.Bool(channel.linearFilter);w.Key("repeatWrap");w.Bool(channel.repeatWrap);
                    w.Key("mipmaps");w.Bool(channel.mipmaps);w.EndObject();}w.EndArray();w.EndObject();}w.EndArray();
            w.Key("textures");w.StartArray();for(const auto& texture:package->textures){
                w.StartObject();w.Key("id");w.String(texture.id.c_str());w.Key("fingerprint");w.String(texture.fingerprint.c_str());
                w.Key("seed");w.Uint(texture.seed);w.Key("generated");w.Bool(texture.encoding==iris::ShaderSceneTextureEncoding::HashV1);
                w.Key("encoding");w.String(texture.encoding==iris::ShaderSceneTextureEncoding::HashV1?"hash-v1":
                    texture.encoding==iris::ShaderSceneTextureEncoding::Png?"png":"raw");
                w.Key("format");w.String(texture.format==iris::ShaderSceneTextureFormat::LinearR8?"linear-r8":
                    texture.format==iris::ShaderSceneTextureFormat::SrgbRgba8?"srgb-rgba8":"linear-rgba8");
                w.Key("extent");w.StartArray();w.Int(texture.width);w.Int(texture.height);w.Int(texture.depth);w.EndArray();
                w.Key("volume");w.Bool(texture.volume);w.Key("mipmaps");w.Bool(texture.mipmaps);
                w.Key("path");w.String(texture.path.generic_u8string().c_str());w.EndObject();}w.EndArray();
            w.EndObject();
        }
        w.Key("shaderMaterials");w.StartArray();
        if (scene) for (const auto& entity : scene->entities) if (!entity.shaderMaterial.resource.empty()) {
            iris::ShaderScenePackage material;std::string materialError;
            if (!iris::loadShaderScenePackage(std::filesystem::u8path(entity.shaderMaterial.resource),material,materialError))
                throw std::runtime_error(materialError);
            const auto values=iris::shaderMaterialValues(material,entity.shaderMaterial);
            w.StartObject();w.Key("entity");w.Uint64(entity.id);w.Key("id");w.String(material.id.c_str());
            w.Key("version");w.Int(material.version);w.Key("resource");w.String(material.manifest.generic_u8string().c_str());
            w.Key("author");w.String(material.author.c_str());w.Key("license");w.String(material.license.c_str());
            w.Key("parameters");w.StartObject();
            for (std::size_t i=0;i<values.size();++i) {w.Key(material.parameters.descriptors()[i].id.c_str());w.Double(values[i].number);}
            w.EndObject();w.EndObject();
        }
        w.EndArray();
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
            w.StartObject();w.Key("path");w.String(input.path.generic_u8string().c_str());
            w.Key("exists");w.Bool(input.exists);w.Key("bytes");w.Uint64(input.bytes);
            w.Key("fingerprint");w.String(input.fingerprint.c_str());w.EndObject();}
        w.EndArray();w.EndObject();
        w.Key("module");w.String(job.module.id.c_str());
        w.Key("moduleSeed");w.Uint(job.module.seed);
        if(moduleReport) { w.Key("moduleRun"); writeModuleRunReportJson(w, *moduleReport); }
        w.Key("samplingSeed");w.Uint(job.renderSettings.seed);
        w.Key("capture");w.StartObject();
        w.Key("determinism");w.Bool(job.rasterDeterminism);
        w.Key("warmupFrames");w.Int(job.rasterWarmupFrames);
        w.Key("renderedFramesAtFixedTime");w.Int(job.rasterWarmupFrames+1);
        w.Key("temporalAccumulation");w.Bool(job.rasterTemporalAccumulation);
        w.Key("historyResetPerOutput");w.Bool(!job.shaderSceneSequence);
        w.Key("effectiveTaa");w.Bool(!settings.vulkanRaster && (settings.enscapeCubeShaderEnabled || (!job.rasterDeterminism && settings.temporalAaEnabled)));
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
