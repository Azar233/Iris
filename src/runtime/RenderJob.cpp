#include "asset/InputManifest.h"
#include "runtime/RenderJob.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

#define RAPIDJSON_NAMESPACE myrenderer_render_job_json
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#undef RAPIDJSON_NAMESPACE
#undef RAPIDJSON_NAMESPACE_BEGIN
#undef RAPIDJSON_NAMESPACE_END

namespace job_json = myrenderer_render_job_json;

namespace {

std::filesystem::path resolved(const std::filesystem::path& value,
                               const std::filesystem::path& jobPath) {
    if (value.empty() || value.is_absolute()) return value.lexically_normal();
    return (jobPath.parent_path() / value).lexically_normal();
}

const job_json::Value& required(const job_json::Value& object, const char* name,
                                job_json::Type type) {
    if (!object.IsObject() || !object.HasMember(name) || object[name].GetType() != type) {
        throw std::runtime_error(std::string("Render Job requires '") + name + "'");
    }
    return object[name];
}

int integer(const job_json::Value& object, const char* name) {
    const auto& value = required(object, name, job_json::kNumberType);
    if (!value.IsInt()) throw std::runtime_error(std::string("'") + name + "' must be an integer");
    return value.GetInt();
}

pathtracer::RenderOutput outputFromName(const std::string& name) {
    if (name == "beauty") return pathtracer::RenderOutput::Beauty;
    if (name == "albedo") return pathtracer::RenderOutput::Albedo;
    if (name == "normal") return pathtracer::RenderOutput::Normal;
    if (name == "depth") return pathtracer::RenderOutput::Depth;
    if (name == "direct") return pathtracer::RenderOutput::Direct;
    if (name == "indirect") return pathtracer::RenderOutput::Indirect;
    if (name == "sample-count") return pathtracer::RenderOutput::SampleCount;
    if (name == "variance") return pathtracer::RenderOutput::Variance;
    throw std::runtime_error("Unknown Render Job AOV: " + name);
}

pathtracer::RenderFileFormat formatFromName(const std::string& name) {
    if (name == "png") return pathtracer::RenderFileFormat::Png;
    if (name == "hdr") return pathtracer::RenderFileFormat::RadianceHdr;
    if (name == "exr") return pathtracer::RenderFileFormat::OpenExr;
    throw std::runtime_error("Unknown Render Job output format: " + name);
}

// Module parameters persist as neutral JSON scalars; the module's descriptor decides
// what the value means. A triple is a colour, a string is an enum label or an asset
// path, and numbers keep their integer/float distinction.
ModuleParameterOverride parameterFromJson(const std::string& id, const job_json::Value& value) {
    ModuleParameterOverride entry;
    entry.id = id;
    if (value.IsBool()) {
        entry.value.type = ModuleParameterType::Bool;
        entry.value.boolean = value.GetBool();
        return entry;
    }
    if (value.IsInt()) {
        entry.value.type = ModuleParameterType::Int;
        entry.value.integer = value.GetInt();
        entry.value.number = static_cast<float>(value.GetInt());
        return entry;
    }
    if (value.IsNumber()) {
        entry.value.type = ModuleParameterType::Float;
        entry.value.number = value.GetFloat();
        return entry;
    }
    if (value.IsString()) {
        entry.value.type = ModuleParameterType::Asset;
        entry.value.text = value.GetString();
        return entry;
    }
    if (value.IsArray() && value.Size() == 3U
        && value[0].IsNumber() && value[1].IsNumber() && value[2].IsNumber()) {
        entry.value.type = ModuleParameterType::Color;
        entry.value.color = glm::vec3(
            value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat()
        );
        return entry;
    }
    throw std::runtime_error(
        "Module parameter '" + id + "' must be a bool, number, string or [r, g, b]"
    );
}

} // namespace

const char* renderOutputName(pathtracer::RenderOutput output) {
    switch (output) {
        case pathtracer::RenderOutput::Beauty: return "beauty";
        case pathtracer::RenderOutput::Albedo: return "albedo";
        case pathtracer::RenderOutput::Normal: return "normal";
        case pathtracer::RenderOutput::Depth: return "depth";
        case pathtracer::RenderOutput::Direct: return "direct";
        case pathtracer::RenderOutput::Indirect: return "indirect";
        case pathtracer::RenderOutput::SampleCount: return "sample-count";
        case pathtracer::RenderOutput::Variance: return "variance";
    }
    return "unknown";
}

const char* renderFileFormatName(pathtracer::RenderFileFormat format) {
    switch (format) {
        case pathtracer::RenderFileFormat::Png: return "png";
        case pathtracer::RenderFileFormat::RadianceHdr: return "hdr";
        case pathtracer::RenderFileFormat::OpenExr: return "exr";
    }
    return "unknown";
}

const char* renderFileFormatExtension(pathtracer::RenderFileFormat format) {
    switch (format) {
        case pathtracer::RenderFileFormat::Png: return ".png";
        case pathtracer::RenderFileFormat::RadianceHdr: return ".hdr";
        case pathtracer::RenderFileFormat::OpenExr: return ".exr";
    }
    return "";
}

bool loadRenderJob(const std::filesystem::path& path, RenderJob& job, std::string& error) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open Render Job: " + path.string());
        const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        job_json::Document root;
        root.Parse(json.c_str(), json.size());
        if (root.HasParseError()) {
            throw std::runtime_error(std::string("Render Job JSON parse error: ")
                + job_json::GetParseError_En(root.GetParseError()));
        }
        if (!root.IsObject()) throw std::runtime_error("Render Job root must be an object");
        if (std::string(required(root, "format", job_json::kStringType).GetString())
            != "MyRendererRenderJob") {
            throw std::runtime_error("Render Job format must be MyRendererRenderJob");
        }

        RenderJob loaded;
        loaded.loadedSourceFingerprint = capture::fingerprintBytes(json);
        loaded.schemaVersion = integer(root, "schemaVersion");
        loaded.sourcePath = std::filesystem::absolute(path).lexically_normal();
        loaded.scenePath = resolved(required(root, "scene", job_json::kStringType).GetString(), loaded.sourcePath);
        loaded.renderer = required(root, "renderer", job_json::kStringType).GetString();
        if (root.HasMember("raster")) {
            if (loaded.schemaVersion < 3 || loaded.renderer != "raster")
                throw std::runtime_error("'raster' capture settings require a schema 3 raster job");
            const auto& capture = required(root, "raster", job_json::kObjectType);
            if (!capture.HasMember("determinism") || !capture["determinism"].IsBool()
                || !capture.HasMember("temporalAccumulation") || !capture["temporalAccumulation"].IsBool())
                throw std::runtime_error("raster determinism and temporalAccumulation must be boolean");
            loaded.rasterDeterminism = capture["determinism"].GetBool();
            loaded.rasterWarmupFrames = integer(capture, "warmupFrames");
            loaded.rasterTemporalAccumulation = capture["temporalAccumulation"].GetBool();
        } else if (loaded.schemaVersion >= 3 && loaded.renderer == "raster") {
            throw std::runtime_error("Schema 3 raster jobs require explicit raster capture settings");
        }
        if (root.HasMember("camera")) loaded.camera = required(root, "camera", job_json::kStringType).GetString(); // capture schema checked

        const auto& resolution = required(root, "resolution", job_json::kArrayType);
        if (resolution.Size() != 2U || !resolution[0].IsUint() || !resolution[1].IsUint()) {
            throw std::runtime_error("'resolution' must be [positive width, positive height]");
        }
        loaded.renderSettings.width = resolution[0].GetUint();
        loaded.renderSettings.height = resolution[1].GetUint();

        const auto& frames = required(root, "frames", job_json::kObjectType);
        loaded.startFrame = integer(frames, "start");
        loaded.endFrame = integer(frames, "end");
        loaded.framesPerSecond = integer(frames, "fps");

        const auto& sampling = required(root, "sampling", job_json::kObjectType);
        const int spp = integer(sampling, "spp");
        const int depth = integer(sampling, "maxDepth");
        const int seed = integer(sampling, "seed");
        if (spp > 0) loaded.renderSettings.samplesPerPixel = static_cast<std::uint32_t>(spp);
        if (depth > 0) loaded.renderSettings.maxDepth = static_cast<std::uint32_t>(depth);
        if (seed >= 0) loaded.renderSettings.seed = static_cast<std::uint32_t>(seed);

        const auto& aovs = required(root, "aovs", job_json::kArrayType);
        loaded.outputs.clear();
        for (const auto& value : aovs.GetArray()) {
            if (!value.IsString()) throw std::runtime_error("Every AOV must be a string");
            loaded.outputs.push_back(outputFromName(value.GetString()));
        }

        const auto& output = required(root, "output", job_json::kObjectType);
        loaded.outputStemPattern = resolved(required(output, "path", job_json::kStringType).GetString(), loaded.sourcePath);
        if (output.HasMember("resume")) {
            if (!output["resume"].IsBool()) throw std::runtime_error("'output.resume' must be boolean");
            loaded.resume = output["resume"].GetBool();
        }
        const auto& formats = required(output, "formats", job_json::kArrayType);
        std::set<std::string> formatNames;
        loaded.outputFormats.clear();
        for (const auto& value : formats.GetArray()) {
            if (!value.IsString()) throw std::runtime_error("Every output format must be a string");
            const std::string name = value.GetString();
            if (!formatNames.insert(name).second) {
                throw std::runtime_error("Render Job output formats must be unique");
            }
            loaded.outputFormats.push_back(formatFromName(name));
        }

        if (root.HasMember("module")) {
            const auto& module = required(root, "module", job_json::kObjectType);
            loaded.module.id = required(module, "id", job_json::kStringType).GetString();
            if (module.HasMember("seed")) {
                if (!module["seed"].IsUint()) throw std::runtime_error("'module.seed' must be an unsigned integer");
                loaded.module.seed = module["seed"].GetUint();
            }
            if (module.HasMember("parameters")) {
                const auto& parameters = required(module, "parameters", job_json::kObjectType);
                for (auto entry = parameters.MemberBegin(); entry != parameters.MemberEnd(); ++entry) {
                    loaded.module.parameters.push_back(
                        parameterFromJson(entry->name.GetString(), entry->value)
                    );
                }
            }
        }

        if (root.HasMember("simulationCache")) {
            loaded.simulationCache = resolved(required(root, "simulationCache", job_json::kStringType).GetString(), loaded.sourcePath);
        }
        if (root.HasMember("failurePolicy")) {
            const std::string policy = required(root, "failurePolicy", job_json::kStringType).GetString();
            if (policy == "stop") loaded.failurePolicy = RenderJobFailurePolicy::Stop;
            else if (policy == "continue") loaded.failurePolicy = RenderJobFailurePolicy::Continue;
            else throw std::runtime_error("failurePolicy must be 'stop' or 'continue'");
        }

        std::string validationError;
        if (!validateRenderJob(loaded, validationError)) throw std::runtime_error(validationError);
        job = std::move(loaded);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool validateQueueRenderJob(const RenderJob& job, std::string& error) {
    if (!validateRenderJob(job, error)) return false;
    if (job.renderer != "cpu-path-traced") {
        error = "Render Queue supports CPU jobs only; run Raster jobs with Iris raster-sequence <job.renderjob>";
        return false;
    }
    return true;
}

bool validateRenderJob(const RenderJob& job, std::string& error) {
    if (job.schemaVersion < RenderJob::minimumSchemaVersion
        || job.schemaVersion > RenderJob::currentSchemaVersion) {
        error = "Unsupported Render Job schemaVersion " + std::to_string(job.schemaVersion);
    } else if (job.schemaVersion == 1 && !job.module.id.empty()) {
        error = "Render Job schemaVersion 1 cannot carry a module; use schemaVersion 2";
    } else if (!job.module.id.empty() && job.module.id.find_first_of(" \t\n") != std::string::npos) {
        error = "Render Job module id must not contain whitespace";
    } else if (job.renderer != "cpu-path-traced" && job.renderer != "raster") {
        error = "Render Job renderer must be 'cpu-path-traced' or 'raster'";
    } else if (job.renderer == "raster"
               && (job.outputs.size() != 1U || job.outputs.front() != pathtracer::RenderOutput::Beauty
                   || job.outputFormats.size() != 1U
                   || job.outputFormats.front() != pathtracer::RenderFileFormat::Png)) {
        error = "Raster Render Job supports beauty PNG output only";
    } else if (job.renderer == "raster" && job.resume) {
        error = "Raster Render Job resume is unavailable until output manifests are verified";
    } else if (job.renderer == "raster" && !job.simulationCache.empty()) {
        error = "Raster Render Job does not consume a simulation cache";
    } else if (!job.simulationCache.empty() && job.module.id.empty()) {
        error = "A simulation cache requires an explicit Render Job module";
    } else if (job.rasterWarmupFrames < 0 || job.rasterWarmupFrames > 240) {
        error = "Raster warmupFrames must be within 0..240";
    } else if (job.renderer == "raster" && job.rasterDeterminism && job.rasterTemporalAccumulation) {
        error = "Raster determinism cannot enable temporal accumulation";
    } else if (job.camera != "scene") { // capture settings checked
        error = "P1-0B v1 supports camera 'scene' only";
    } else if (!std::filesystem::is_regular_file(job.scenePath)) {
        error = "Render Job scene does not exist: " + job.scenePath.string();
    } else if (job.renderSettings.width == 0U || job.renderSettings.height == 0U
               || job.renderSettings.width > 16384U || job.renderSettings.height > 16384U) {
        error = "Render Job resolution must be within 1..16384";
    } else if (job.renderSettings.samplesPerPixel == 0U || job.renderSettings.maxDepth == 0U) {
        error = "Render Job sampling values must be positive";
    } else if (job.startFrame < 0 || job.endFrame < job.startFrame || job.framesPerSecond < 1
               || job.framesPerSecond > 240) {
        error = "Render Job frame range/FPS is invalid";
    } else if (job.outputs.empty()) {
        error = "Render Job must request at least one AOV";
    } else if (job.outputFormats.empty()) {
        error = "Render Job must request at least one output format";
    } else if (std::set<pathtracer::RenderFileFormat>(
                   job.outputFormats.begin(), job.outputFormats.end()).size()
               != job.outputFormats.size()) {
        error = "Render Job output formats must be unique";
    } else if (job.outputStemPattern.empty()) {
        error = "Render Job output.path must not be empty";
    } else if (job.startFrame != job.endFrame
               && job.outputStemPattern.string().find("{frame") == std::string::npos) {
        error = "Render sequence output.path must contain {frame} or {frame:NN}";
    } else {
        error.clear();
        return true;
    }
    return false;
}

bool applyRenderJobOutputOverride(RenderJob& job,
                                  const std::filesystem::path& outputStemPattern,
                                  std::string& error) {
    RenderJob overridden = job;
    overridden.outputStemPattern = outputStemPattern.lexically_normal();
    if (!validateRenderJob(overridden, error)) return false;
    job = std::move(overridden);
    error.clear();
    return true;
}

std::filesystem::path renderJobFrameStem(const RenderJob& job, int frame) {
    std::string value = job.outputStemPattern.string();
    const std::size_t begin = value.find("{frame");
    if (begin == std::string::npos) return job.outputStemPattern;
    const std::size_t end = value.find('}', begin);
    if (end == std::string::npos) throw std::runtime_error("Unterminated frame token in output path");
    int width = 0;
    if (value.compare(begin, 7U, "{frame:") == 0) {
        const std::string widthText = value.substr(begin + 7U, end - begin - 7U);
        std::size_t parsed = 0U;
        width = std::stoi(widthText, &parsed);
        if (parsed != widthText.size() || width < 1 || width > 12) {
            throw std::runtime_error("Frame token width must be within 1..12");
        }
    } else if (value.compare(begin, 7U, "{frame}") != 0) {
        throw std::runtime_error("Frame token must be {frame} or {frame:NN}");
    }
    std::ostringstream formatted;
    if (width > 0) formatted << std::setw(width) << std::setfill('0');
    formatted << frame;
    value.replace(begin, end - begin + 1U, formatted.str());
    return std::filesystem::path(value);
}
