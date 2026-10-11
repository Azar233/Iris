#include "plugin/ShaderScene.h"
#include "asset/InputManifest.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <map>
#define RAPIDJSON_NAMESPACE iris_shader_scene_json
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#undef RAPIDJSON_NAMESPACE
#undef RAPIDJSON_NAMESPACE_BEGIN
#undef RAPIDJSON_NAMESPACE_END

namespace iris {
namespace {
using Value = iris_shader_scene_json::Value;
void fields(const Value& value, std::initializer_list<const char*> names) {
    if (!value.IsObject()) throw std::runtime_error("Object required");
    std::unordered_set<std::string> seen;
    for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
        const std::string key(it->name.GetString(), it->name.GetStringLength());
        bool known = false;
        for (const auto* name : names) known = known || key == name;
        if (!known || !seen.insert(key).second) throw std::runtime_error("Unknown or duplicate field: " + key);
    }
}
std::string string(const Value& value, const char* key) {
    if (!value.HasMember(key) || !value[key].IsString()) throw std::runtime_error(std::string("String required: ") + key);
    const std::string result(value[key].GetString(), value[key].GetStringLength());
    if (result.empty() || result.size() > 4096 || result.find('\0') != std::string::npos)
        throw std::runtime_error(std::string("Invalid string: ") + key);
    return result;
}
bool identifier(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (const auto c : id) if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z')
        && !(c >= '0' && c <= '9') && c != '_') return false;
    return !(id[0] >= '0' && id[0] <= '9') && id.rfind("gl_", 0) != 0 && id.rfind("iris", 0) != 0;
}
std::filesystem::path inside(const std::filesystem::path& base, const std::string& name) {
    const auto relative = std::filesystem::u8path(name);
    if (relative.is_absolute() || relative.has_root_name() || name.find(':') != std::string::npos)
        throw std::runtime_error("Package paths must be relative: " + name);
    const auto resolved = std::filesystem::canonical(base / relative);
    const auto within = resolved.lexically_relative(base);
    if (within.empty() || *within.begin() == "..") throw std::runtime_error("Package path escapes resource root: " + name);
    return resolved;
}
float number(const Value& value, const char* key) {
    if (!value.HasMember(key) || !value[key].IsNumber()) throw std::runtime_error(std::string("Number required: ") + key);
    const double result = value[key].GetDouble();
    if (!std::isfinite(result) || std::abs(result) > 1.0e6) throw std::runtime_error("Parameter number out of range");
    return static_cast<float>(result);
}
std::filesystem::path fragmentPath(const std::filesystem::path& root, const std::string& name) {
    const auto path = inside(root, name);
    if (!std::filesystem::is_regular_file(path) || (path.extension() != ".frag" && path.extension() != ".glsl")
        || std::filesystem::file_size(path) > 1048576) throw std::runtime_error("Invalid or oversized fragment file: " + name);
    return path;
}
void resolveGraph(ShaderScenePackage& package) {
    std::map<std::string, std::size_t> indices;
    for (std::size_t i = 0; i < package.passes.size(); ++i)
        if (!indices.emplace(package.passes[i].id, i).second) throw std::runtime_error("Duplicate Pass ID");
    if (!indices.count("Image")) throw std::runtime_error("Exactly one Image Pass is required");
    for (auto& pass : package.passes) for (auto& channel : pass.channels) {
        const auto source = indices.find(channel.source);
        if (source == indices.end() || channel.source == "Image") throw std::runtime_error("Channel source must name a buffer Pass: " + channel.source);
        channel.sourcePass = source->second;
    }
    std::vector<bool> ready(package.passes.size(), false);
    while (package.executionOrder.size() < package.passes.size()) {
        bool progress = false;
        for (std::size_t i = 0; i < package.passes.size(); ++i) {
            if (ready[i]) continue;
            const auto& pass = package.passes[i];
            if (pass.id == "Image" && package.executionOrder.size() + 1 != package.passes.size()) continue;
            bool available = true;
            for (const auto& channel : pass.channels)
                if (!channel.previousFrame && !ready[channel.sourcePass]) available = false;
            if (!available) continue;
            ready[i] = true; package.executionOrder.push_back(i); progress = true;
        }
        if (!progress) throw std::runtime_error("Same-frame Pass cycle rejected (use explicit previous frame)");
    }
}
}
bool loadShaderScenePackage(const std::filesystem::path& path, ShaderScenePackage& package, std::string& error) {
    try {
        ShaderScenePackage loaded;
        loaded.manifest = std::filesystem::canonical(path);
        if (loaded.manifest.extension() != ".shaderscene") throw std::runtime_error("Expected .shaderscene manifest");
        if (std::filesystem::file_size(loaded.manifest) > 65536) throw std::runtime_error("Manifest exceeds 64 KiB");
        capture::recordInput(loaded.manifest);
        std::ifstream file(loaded.manifest, std::ios::binary);
        std::ostringstream bytes; bytes << file.rdbuf();
        iris_shader_scene_json::Document json;
        const auto source = bytes.str(); json.Parse(source.data(), source.size());
        if (json.HasParseError()) throw std::runtime_error(std::string("JSON: ") + iris_shader_scene_json::GetParseError_En(json.GetParseError())
            + " at byte " + std::to_string(json.GetErrorOffset()));
        if (!json.IsObject() || !json.HasMember("version") || !json["version"].IsInt()
            || (json["version"].GetInt() != 1 && json["version"].GetInt() != 2))
            throw std::runtime_error("Unsupported ShaderScene version (expected 1 or 2)");
        loaded.version = json["version"].GetInt();
        if (loaded.version == 1) fields(json, {"version", "id", "resourceRoot", "fragment", "entry", "output", "camera", "author", "license", "parameters"});
        else fields(json, {"version", "id", "resourceRoot", "entry", "camera", "author", "license", "parameters", "passes"});
        loaded.id = string(json, "id");
        if (!identifier(loaded.id)) throw std::runtime_error("Invalid package id");
        loaded.author = string(json, "author"); loaded.license = string(json, "license");
        const auto entry = string(json, "entry");
        loaded.mainImageEntry = loaded.version == 2 && entry == "mainImage";
        if ((entry != "main" && !loaded.mainImageEntry) || (loaded.version == 1 && string(json, "output") != "display-rgba8")
            || string(json, "camera") != "project") throw std::runtime_error("Unsupported entry/output/camera contract");
        loaded.root = inside(loaded.manifest.parent_path(), string(json, "resourceRoot"));
        if (!std::filesystem::is_directory(loaded.root)) throw std::runtime_error("Resource root must be a directory");
        if (loaded.version == 1) {
            ShaderScenePass pass; pass.id = "Image"; pass.fragment = fragmentPath(loaded.root, string(json, "fragment"));
            loaded.passes.push_back(std::move(pass));
        } else {
            if (!json.HasMember("passes") || !json["passes"].IsArray() || json["passes"].Empty()
                || json["passes"].Size() > shaderSceneMaximumPasses) throw std::runtime_error("passes must contain 1..4 Passes");
            for (const auto& value : json["passes"].GetArray()) {
                fields(value, {"id", "fragment", "output", "sizeDivisor", "channels"});
                ShaderScenePass pass; pass.id = string(value, "id");
                if (!identifier(pass.id)) throw std::runtime_error("Invalid Pass ID: " + pass.id);
                pass.fragment = fragmentPath(loaded.root, string(value, "fragment"));
                const auto output = string(value, "output");
                if (output != (pass.id == "Image" ? "display-rgba8" : "linear-rgba16f")) throw std::runtime_error("Invalid output format for Pass " + pass.id);
                pass.format = pass.id == "Image" ? ShaderSceneFormat::DisplayRgba8 : ShaderSceneFormat::LinearRgba16f;
                if (!value.HasMember("sizeDivisor") || !value["sizeDivisor"].IsInt()
                    || (value["sizeDivisor"].GetInt() != 1 && value["sizeDivisor"].GetInt() != 2)
                    || (pass.id == "Image" && value["sizeDivisor"].GetInt() != 1)) throw std::runtime_error("Invalid Pass sizeDivisor");
                pass.sizeDivisor = value["sizeDivisor"].GetInt();
                if (!value.HasMember("channels") || !value["channels"].IsArray() || value["channels"].Size() > shaderSceneMaximumChannels)
                    throw std::runtime_error("channels must contain at most 4 entries");
                std::unordered_set<int> slots;
                for (const auto& input : value["channels"].GetArray()) {
                    fields(input, {"slot", "source", "frame", "filter", "wrap", "mip"});
                    ShaderSceneChannel channel;
                    if (!input.HasMember("slot") || !input["slot"].IsInt() || input["slot"].GetInt() < 0 || input["slot"].GetInt() >= 4
                        || !slots.insert(input["slot"].GetInt()).second) throw std::runtime_error("Invalid/duplicate channel slot");
                    channel.slot = input["slot"].GetInt(); channel.source = string(input, "source");
                    const auto frame = string(input, "frame"), filter = string(input, "filter"), wrap = string(input, "wrap");
                    if ((frame != "current" && frame != "previous") || (filter != "nearest" && filter != "linear")
                        || (wrap != "clamp" && wrap != "repeat") || string(input, "mip") != "none") throw std::runtime_error("Unsupported channel frame/filter/wrap/mip");
                    channel.previousFrame = frame == "previous"; channel.linearFilter = filter == "linear"; channel.repeatWrap = wrap == "repeat";
                    pass.channels.push_back(std::move(channel));
                }
                loaded.passes.push_back(std::move(pass));
            }
        }
        resolveGraph(loaded);
        if (!json.HasMember("parameters") || !json["parameters"].IsArray() || json["parameters"].Size() > 16)
            throw std::runtime_error("parameters must be an array of at most 16 floats");
        for (const auto& parameter : json["parameters"].GetArray()) {
            fields(parameter, {"id", "label", "type", "default", "min", "max"});
            const auto id = string(parameter, "id");
            if (!identifier(id) || loaded.parameters.contains(id) || string(parameter, "type") != "float")
                throw std::runtime_error("Invalid, duplicate or unsupported parameter: " + id);
            const auto minimum = number(parameter, "min"), maximum = number(parameter, "max"), initial = number(parameter, "default");
            if (minimum > maximum || initial < minimum || initial > maximum) throw std::runtime_error("Invalid parameter range: " + id);
            loaded.parameters.registerFloat(id, string(parameter, "label"), initial, minimum, maximum);
        }
        package = std::move(loaded); error.clear(); return true;
    } catch (const std::exception& exception) { error = "ShaderScene " + path.string() + ": " + exception.what(); return false; }
}
}
