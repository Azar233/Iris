#include "module/SimulationCache.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#define RAPIDJSON_NAMESPACE myrenderer_simulation_cache_json
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#undef RAPIDJSON_NAMESPACE
#undef RAPIDJSON_NAMESPACE_BEGIN
#undef RAPIDJSON_NAMESPACE_END

namespace cache_json = myrenderer_simulation_cache_json;

namespace {

void writeVector(cache_json::PrettyWriter<cache_json::StringBuffer>& writer, const glm::vec3& value) {
    writer.StartArray();
    writer.Double(static_cast<double>(value.x));
    writer.Double(static_cast<double>(value.y));
    writer.Double(static_cast<double>(value.z));
    writer.EndArray();
}

bool readVector(const cache_json::Value& value, glm::vec3& out) {
    if (!value.IsArray() || value.Size() != 3U) return false;
    if (!value[0].IsNumber() || !value[1].IsNumber() || !value[2].IsNumber()) return false;
    out = glm::vec3(value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat());
    return true;
}

std::string describe(const SimulationCacheKey& key) {
    return "scene=" + std::to_string(key.sceneContentHash)
        + ", module=" + (key.moduleId.empty() ? std::string("<none>") : key.moduleId)
        + ", api=" + std::to_string(key.moduleApiVersion)
        + ", build=" + key.buildId
        + ", seed=" + std::to_string(key.seed)
        + ", fps=" + std::to_string(key.framesPerSecond)
        + ", frames=" + std::to_string(key.startFrame) + ".." + std::to_string(key.endFrame);
}

} // namespace

const char* simulationCacheStatusName(SimulationCacheStatus status) {
    switch (status) {
        case SimulationCacheStatus::Disabled: return "Disabled";
        case SimulationCacheStatus::Missing: return "Missing";
        case SimulationCacheStatus::Stale: return "Stale";
        case SimulationCacheStatus::Hit: return "Hit";
    }
    return "Unknown";
}

bool SimulationCacheKey::operator==(const SimulationCacheKey& other) const {
    return sceneContentHash == other.sceneContentHash
        && moduleId == other.moduleId
        && moduleApiVersion == other.moduleApiVersion
        && buildId == other.buildId
        && seed == other.seed
        && parameterHash == other.parameterHash
        && framesPerSecond == other.framesPerSecond
        && startFrame == other.startFrame
        && endFrame == other.endFrame;
}

std::string SimulationCacheKey::describeDifferences(const SimulationCacheKey& other) const {
    std::string differences;
    const auto append = [&differences](const std::string& text) {
        if (!differences.empty()) differences += "; ";
        differences += text;
    };
    if (sceneContentHash != other.sceneContentHash) {
        append("scene content hash changed (" + std::to_string(sceneContentHash)
               + " -> " + std::to_string(other.sceneContentHash) + ")");
    }
    if (moduleId != other.moduleId) {
        append("module id changed (" + moduleId + " -> " + other.moduleId + ")");
    }
    if (moduleApiVersion != other.moduleApiVersion) {
        append("module API version changed (" + std::to_string(moduleApiVersion)
               + " -> " + std::to_string(other.moduleApiVersion) + ")");
    }
    if (buildId != other.buildId) {
        append("build id changed (" + buildId + " -> " + other.buildId + ")");
    }
    if (seed != other.seed) {
        append("seed changed (" + std::to_string(seed) + " -> " + std::to_string(other.seed) + ")");
    }
    if (parameterHash != other.parameterHash) {
        append("module parameters changed");
    }
    if (framesPerSecond != other.framesPerSecond) {
        append("fixed step changed (" + std::to_string(framesPerSecond)
               + " -> " + std::to_string(other.framesPerSecond) + " fps)");
    }
    if (startFrame != other.startFrame || endFrame != other.endFrame) {
        append("frame range changed (" + std::to_string(startFrame) + ".."
               + std::to_string(endFrame) + " -> " + std::to_string(other.startFrame)
               + ".." + std::to_string(other.endFrame) + ")");
    }
    return differences;
}

const SimulationCacheFrame* SimulationCache::find(int frame) const {
    for (const SimulationCacheFrame& entry : frames) {
        if (entry.frame == frame) return &entry;
    }
    return nullptr;
}

bool SimulationCache::covers(int frame) const {
    return find(frame) != nullptr;
}

SimulationCacheStatus classifySimulationCache(
    const SimulationCache& cache,
    const SimulationCacheKey& requested,
    std::string& message
) {
    if (cache.key == requested) {
        if (!cache.covers(requested.startFrame) || !cache.covers(requested.endFrame)) {
            message = "Simulation cache key matches but the frame range is incomplete";
            return SimulationCacheStatus::Stale;
        }
        message = "Simulation cache matches: " + describe(requested);
        return SimulationCacheStatus::Hit;
    }
    const std::string differences = cache.key.describeDifferences(requested);
    message = differences.empty()
        ? "Simulation cache key differs"
        : "Simulation cache is stale: " + differences;
    return SimulationCacheStatus::Stale;
}

bool saveSimulationCache(
    const std::filesystem::path& path,
    const SimulationCache& cache,
    std::string& error
) {
    try {
        if (path.empty()) throw std::runtime_error("Simulation cache path must not be empty");
        const std::filesystem::path temporary(path.string() + ".partial");
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }

        cache_json::StringBuffer buffer;
        cache_json::PrettyWriter<cache_json::StringBuffer> writer(buffer);
        writer.SetIndent(' ', 2);
        writer.StartObject();
        writer.Key("format");
        writer.String("MyRendererSimulationCache");
        writer.Key("schemaVersion");
        writer.Int(SimulationCache::currentSchemaVersion);
        writer.Key("key");
        writer.StartObject();
        writer.Key("sceneContentHash");
        writer.Uint64(cache.key.sceneContentHash);
        writer.Key("moduleId");
        writer.String(cache.key.moduleId.c_str());
        writer.Key("moduleApiVersion");
        writer.Int(cache.key.moduleApiVersion);
        writer.Key("buildId");
        writer.String(cache.key.buildId.c_str());
        writer.Key("seed");
        writer.Uint(cache.key.seed);
        writer.Key("parameterHash");
        writer.Uint64(cache.key.parameterHash);
        writer.Key("framesPerSecond");
        writer.Int(cache.key.framesPerSecond);
        writer.Key("startFrame");
        writer.Int(cache.key.startFrame);
        writer.Key("endFrame");
        writer.Int(cache.key.endFrame);
        writer.EndObject();
        writer.Key("frames");
        writer.StartArray();
        for (const SimulationCacheFrame& frame : cache.frames) {
            writer.StartObject();
            writer.Key("frame");
            writer.Int(frame.frame);
            writer.Key("contentHash");
            writer.Uint64(frame.contentHash);
            writer.Key("moduleState");
            writer.String(frame.moduleState.c_str());
            writer.Key("entities");
            writer.StartArray();
            for (const SimulationCacheEntity& entity : frame.entities) {
                writer.StartObject();
                writer.Key("id");
                writer.Uint64(entity.id);
                writer.Key("translation");
                writeVector(writer, entity.transform.translation);
                writer.Key("rotationDegrees");
                writeVector(writer, entity.transform.rotationDegrees);
                writer.Key("scale");
                writeVector(writer, entity.transform.scale);
                writer.Key("assetTransform");
                writer.StartArray();
                for (int column = 0; column < 4; ++column) {
                    for (int row = 0; row < 4; ++row) {
                        writer.Double(static_cast<double>(entity.transform.assetTransform[column][row]));
                    }
                }
                writer.EndArray();
                writer.Key("tint");
                writeVector(writer, entity.tint);
                if (entity.lightRecorded) {
                    writer.Key("light");
                    if (!entity.light) writer.Null();
                    else {
                        if (!validSceneLight(*entity.light)) throw std::runtime_error("Invalid cached light");
                        const auto& light = *entity.light;
                        writer.StartObject();
                        writer.Key("type"); writer.Int(static_cast<int>(light.type));
                        writer.Key("color"); writeVector(writer, light.color);
                        writer.Key("intensity"); writer.Double(light.intensity);
                        writer.Key("range"); writer.Double(light.range);
                        writer.Key("outerAngleDegrees"); writer.Double(light.outerAngleDegrees);
                        writer.EndObject();
                    }
                }
                writer.EndObject();
            }
            writer.EndArray();
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();

        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Cannot write simulation cache: " + temporary.string());
        }
        output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
        output.close();
        if (!output) throw std::runtime_error("Failed while writing the simulation cache");
        std::filesystem::rename(temporary, path);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool loadSimulationCache(
    const std::filesystem::path& path,
    SimulationCache& cache,
    std::string& error
) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open simulation cache: " + path.string());
        const std::string json((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
        cache_json::Document root;
        root.Parse(json.c_str(), json.size());
        if (root.HasParseError()) {
            throw std::runtime_error(std::string("Simulation cache JSON parse error: ")
                + cache_json::GetParseError_En(root.GetParseError()));
        }
        if (!root.IsObject() || !root.HasMember("format") || !root["format"].IsString()
            || std::string(root["format"].GetString()) != "MyRendererSimulationCache") {
            throw std::runtime_error("Not a MyRenderer simulation cache: " + path.string());
        }
        if (!root.HasMember("schemaVersion") || !root["schemaVersion"].IsInt()) {
            throw std::runtime_error("Simulation cache has no schema version");
        }

        SimulationCache loaded;
        loaded.schemaVersion = root["schemaVersion"].GetInt();
        if (loaded.schemaVersion != SimulationCache::currentSchemaVersion) {
            throw std::runtime_error(
                "Unsupported simulation cache schemaVersion "
                + std::to_string(loaded.schemaVersion)
            );
        }
        if (!root.HasMember("key") || !root["key"].IsObject()) {
            throw std::runtime_error("Simulation cache has no key");
        }
        const auto& key = root["key"];
        if (!key.HasMember("sceneContentHash") || !key["sceneContentHash"].IsUint64()) {
            throw std::runtime_error("Simulation cache key has no scene content hash");
        }
        loaded.key.sceneContentHash = key["sceneContentHash"].GetUint64();
        loaded.key.moduleId = key.HasMember("moduleId") && key["moduleId"].IsString()
            ? key["moduleId"].GetString() : std::string();
        loaded.key.moduleApiVersion = key.HasMember("moduleApiVersion") && key["moduleApiVersion"].IsInt()
            ? key["moduleApiVersion"].GetInt() : 0;
        loaded.key.buildId = key.HasMember("buildId") && key["buildId"].IsString()
            ? key["buildId"].GetString() : std::string();
        loaded.key.seed = key.HasMember("seed") && key["seed"].IsUint() ? key["seed"].GetUint() : 0U;
        // A cache written before the parameter fingerprint existed loads as 0, which can
        // never equal a real fingerprint: such an entry is stale instead of reused.
        loaded.key.parameterHash =
            key.HasMember("parameterHash") && key["parameterHash"].IsUint64()
                ? key["parameterHash"].GetUint64() : 0U;
        loaded.key.framesPerSecond = key.HasMember("framesPerSecond") && key["framesPerSecond"].IsInt()
            ? key["framesPerSecond"].GetInt() : 0;
        loaded.key.startFrame = key.HasMember("startFrame") && key["startFrame"].IsInt()
            ? key["startFrame"].GetInt() : 0;
        loaded.key.endFrame = key.HasMember("endFrame") && key["endFrame"].IsInt()
            ? key["endFrame"].GetInt() : 0;

        if (root.HasMember("frames")) {
            if (!root["frames"].IsArray()) throw std::runtime_error("Simulation cache frames must be an array");
            for (const auto& frameValue : root["frames"].GetArray()) {
                if (!frameValue.IsObject()) throw std::runtime_error("Simulation cache frame must be an object");
                SimulationCacheFrame frame;
                frame.frame = frameValue.HasMember("frame") && frameValue["frame"].IsInt()
                    ? frameValue["frame"].GetInt() : 0;
                frame.contentHash = frameValue.HasMember("contentHash") && frameValue["contentHash"].IsUint64()
                    ? frameValue["contentHash"].GetUint64() : 0U;
                frame.moduleState = frameValue.HasMember("moduleState") && frameValue["moduleState"].IsString()
                    ? frameValue["moduleState"].GetString() : std::string();
                if (frameValue.HasMember("entities")) {
                    if (!frameValue["entities"].IsArray()) {
                        throw std::runtime_error("Simulation cache frame entities must be an array");
                    }
                    for (const auto& entityValue : frameValue["entities"].GetArray()) {
                        if (!entityValue.IsObject() || !entityValue.HasMember("id")
                            || !entityValue["id"].IsUint64()) {
                            throw std::runtime_error("Simulation cache entity must carry an id");
                        }
                        SimulationCacheEntity entity;
                        entity.id = entityValue["id"].GetUint64();
                        if (entityValue.HasMember("light")) {
                            entity.lightRecorded = true;
                            const auto& value = entityValue["light"];
                            if (!value.IsNull()) {
                                if (!value.IsObject() || !value.HasMember("type") || !value["type"].IsInt()
                                    || !value.HasMember("color") || !value.HasMember("intensity") || !value["intensity"].IsNumber()
                                    || !value.HasMember("range") || !value["range"].IsNumber()
                                    || !value.HasMember("outerAngleDegrees") || !value["outerAngleDegrees"].IsNumber())
                                    throw std::runtime_error("Invalid cached light fields");
                                SceneLightComponent light; light.type = static_cast<LocalLightType>(value["type"].GetInt());
                                if (!readVector(value["color"], light.color)) throw std::runtime_error("Invalid cached light color");
                                light.intensity = value["intensity"].GetFloat(); light.range = value["range"].GetFloat();
                                light.outerAngleDegrees = value["outerAngleDegrees"].GetFloat();
                                if (!validSceneLight(light)) throw std::runtime_error("Invalid cached light values");
                                entity.light = light;
                            }
                        }
                        if (!readVector(entityValue["translation"], entity.transform.translation)
                            || !readVector(entityValue["rotationDegrees"], entity.transform.rotationDegrees)
                            || !readVector(entityValue["scale"], entity.transform.scale)
                            || !readVector(entityValue["tint"], entity.tint)) {
                            throw std::runtime_error("Simulation cache entity has an invalid vector");
                        }
                        if (!entityValue.HasMember("assetTransform")
                            || !entityValue["assetTransform"].IsArray()
                            || entityValue["assetTransform"].Size() != 16U) {
                            throw std::runtime_error("Simulation cache entity has no asset transform");
                        }
                        for (int column = 0; column < 4; ++column) {
                            for (int row = 0; row < 4; ++row) {
                                const auto& value = entityValue["assetTransform"][column * 4 + row];
                                if (!value.IsNumber()) {
                                    throw std::runtime_error("Simulation cache asset transform is invalid");
                                }
                                entity.transform.assetTransform[column][row] = value.GetFloat();
                            }
                        }
                        frame.entities.push_back(std::move(entity));
                    }
                }
                loaded.frames.push_back(std::move(frame));
            }
        }
        std::sort(loaded.frames.begin(), loaded.frames.end(),
                  [](const SimulationCacheFrame& left, const SimulationCacheFrame& right) {
                      return left.frame < right.frame;
                  });
        cache = std::move(loaded);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
