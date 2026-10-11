#include "plugin/ShaderScene.h"
#include "asset/InputManifest.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
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
        fields(json, {"version", "id", "resourceRoot", "fragment", "entry", "output", "camera", "author", "license", "parameters"});
        if (!json.HasMember("version") || !json["version"].IsInt() || json["version"].GetInt() != 1)
            throw std::runtime_error("Unsupported ShaderScene version (expected 1)");
        loaded.id = string(json, "id");
        if (!identifier(loaded.id)) throw std::runtime_error("Invalid package id");
        loaded.author = string(json, "author"); loaded.license = string(json, "license");
        if (string(json, "entry") != "main" || string(json, "output") != "display-rgba8"
            || string(json, "camera") != "project") throw std::runtime_error("Unsupported entry/output/camera contract");
        loaded.root = inside(loaded.manifest.parent_path(), string(json, "resourceRoot"));
        if (!std::filesystem::is_directory(loaded.root)) throw std::runtime_error("Resource root must be a directory");
        loaded.fragment = inside(loaded.root, string(json, "fragment"));
        if (!std::filesystem::is_regular_file(loaded.fragment)
            || (loaded.fragment.extension() != ".frag" && loaded.fragment.extension() != ".glsl")
            || std::filesystem::file_size(loaded.fragment) > 1048576) throw std::runtime_error("Invalid or oversized fragment file");
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
