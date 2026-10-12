#include "plugin/ShaderScene.h"
#include "asset/InputManifest.h"
#include "asset/InputWatch.h"
#include "plugin/RenderPluginParameters.h"
#include "render/Renderer.h"
#include <cmath>
#include <algorithm>
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
    if (!std::filesystem::is_directory(base / relative)) capture::watchInput(base / relative);
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
    std::map<std::string, std::size_t> textures;
    for (std::size_t i = 0; i < package.textures.size(); ++i)
        if (indices.count(package.textures[i].id) || !textures.emplace(package.textures[i].id, i).second)
            throw std::runtime_error("Duplicate texture/Pass ID");
    for (auto& pass : package.passes) for (auto& channel : pass.channels) {
        const auto texture = textures.find(channel.source);
        if (texture != textures.end()) {
            channel.externalTexture = true; channel.sourceTexture = texture->second;
            if (channel.previousFrame || (channel.mipmaps && !package.textures[texture->second].mipmaps))
                throw std::runtime_error("Static texture requires current frame and declared Mip chain");
            continue;
        }
        const auto source = indices.find(channel.source);
        if (source == indices.end() || channel.source == "Image") throw std::runtime_error("Channel source must name a buffer Pass or texture: " + channel.source);
        if (channel.mipmaps) throw std::runtime_error("Buffer channels do not support Mip chains");
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
                if (!channel.externalTexture && !channel.previousFrame && !ready[channel.sourcePass]) available = false;
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
        capture::watchInput(path);
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
            || (json["version"].GetInt() < 1 || json["version"].GetInt() > 3))
            throw std::runtime_error("Unsupported ShaderScene version (expected 1..3)");
        loaded.version = json["version"].GetInt();
        if (loaded.version == 1) fields(json, {"version", "id", "resourceRoot", "fragment", "entry", "output", "camera", "author", "license", "parameters"});
        else if (loaded.version == 2) fields(json, {"version", "id", "resourceRoot", "entry", "camera", "author", "license", "parameters", "passes"});
        else fields(json, {"version", "id", "resourceRoot", "entry", "camera", "author", "license", "parameters", "passes", "textures", "parameterSource"});
        loaded.id = string(json, "id");
        if (!identifier(loaded.id)) throw std::runtime_error("Invalid package id");
        loaded.author = string(json, "author"); loaded.license = string(json, "license");
        const auto entry = string(json, "entry");
        loaded.mainImageEntry = loaded.version >= 2 && entry == "mainImage";
        if ((entry != "main" && !loaded.mainImageEntry) || (loaded.version == 1 && string(json, "output") != "display-rgba8")
            || string(json, "camera") != "project") throw std::runtime_error("Unsupported entry/output/camera contract");
        loaded.root = inside(loaded.manifest.parent_path(), string(json, "resourceRoot"));
        if (!std::filesystem::is_directory(loaded.root)) throw std::runtime_error("Resource root must be a directory");
        if (loaded.version == 3) {
            if (!json.HasMember("textures") || !json["textures"].IsArray() || json["textures"].Size() > 8)
                throw std::runtime_error("textures must be an array of at most 8 entries");
            std::size_t bytes = 0;
            for (const auto& value : json["textures"].GetArray()) {
                if (!value.IsObject()) throw std::runtime_error("Texture object required");
                ShaderSceneTexture texture;
                const auto encoding = string(value, "encoding");
                if (encoding == "hash-v1") {
                    fields(value, {"id", "dimension", "format", "extent", "encoding", "seed", "mip"});
                    texture.encoding = ShaderSceneTextureEncoding::HashV1;
                    if (!value.HasMember("seed") || !value["seed"].IsUint()) throw std::runtime_error("Texture Seed must be uint32");
                    texture.seed = value["seed"].GetUint();
                } else {
                    fields(value, {"id", "dimension", "format", "extent", "encoding", "path", "mip"});
                    if (encoding != "png" && encoding != "raw") throw std::runtime_error("Unsupported texture encoding");
                    texture.encoding = encoding == "png" ? ShaderSceneTextureEncoding::Png : ShaderSceneTextureEncoding::Raw;
                    texture.path = inside(loaded.root, string(value, "path"));
                    if (!std::filesystem::is_regular_file(texture.path)) throw std::runtime_error("Texture path must be a file");
                }
                texture.id = string(value, "id");
                if (!identifier(texture.id)) throw std::runtime_error("Invalid texture ID");
                const auto dimension = string(value, "dimension"), format = string(value, "format"), mip = string(value, "mip");
                if (dimension != "2d" && dimension != "3d") throw std::runtime_error("Texture dimension must be 2d/3d");
                texture.volume = dimension == "3d";
                if (format != "linear-r8" && format != "linear-rgba8" && format != "srgb-rgba8") throw std::runtime_error("Unsupported texture format");
                texture.format = format == "linear-r8" ? ShaderSceneTextureFormat::LinearR8
                    : format == "linear-rgba8" ? ShaderSceneTextureFormat::LinearRgba8 : ShaderSceneTextureFormat::SrgbRgba8;
                if ((texture.volume && texture.format == ShaderSceneTextureFormat::SrgbRgba8)
                    || (texture.encoding == ShaderSceneTextureEncoding::Png && (texture.volume || texture.format == ShaderSceneTextureFormat::LinearR8))
                    || (texture.encoding == ShaderSceneTextureEncoding::HashV1 && texture.format != ShaderSceneTextureFormat::LinearR8))
                    throw std::runtime_error("Unsupported texture dimension/format/encoding combination");
                if (mip != "none" && mip != "generate") throw std::runtime_error("Texture Mip must be none/generate");
                texture.mipmaps = mip == "generate";
                if (!value.HasMember("extent") || !value["extent"].IsArray() || value["extent"].Size() != (texture.volume ? 3U : 2U))
                    throw std::runtime_error("Texture extent dimension mismatch");
                int* dimensions[]{&texture.width, &texture.height, &texture.depth};
                for (unsigned int axis = 0; axis < value["extent"].Size(); ++axis) {
                    const auto& extent = value["extent"][axis];
                    if (!extent.IsInt() || extent.GetInt() < 1 || extent.GetInt() > (texture.volume ? 128 : 2048))
                        throw std::runtime_error("Texture extent exceeds 2d 2048 / 3d 128 limit");
                    *dimensions[axis] = extent.GetInt();
                }
                bytes += shaderSceneTextureBytes(texture);
                if (bytes > shaderSceneMaximumTextureBytes) throw std::runtime_error("Texture resources exceed 64 MiB budget");
                loaded.textures.push_back(std::move(texture));
            }
        }
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
                        || (wrap != "clamp" && wrap != "repeat") || (string(input, "mip") != "none"
                            && !(loaded.version == 3 && string(input, "mip") == "linear"))) throw std::runtime_error("Unsupported channel frame/filter/wrap/mip");
                    channel.previousFrame = frame == "previous"; channel.linearFilter = filter == "linear"; channel.repeatWrap = wrap == "repeat";
                    channel.mipmaps = string(input, "mip") == "linear";
                    pass.channels.push_back(std::move(channel));
                }
                loaded.passes.push_back(std::move(pass));
            }
        }
        resolveGraph(loaded);
        for (auto& texture : loaded.textures) loadShaderSceneTextureData(texture);
        if (!json.HasMember("parameters") || !json["parameters"].IsArray() || json["parameters"].Size() > 16)
            throw std::runtime_error("parameters must be an array of at most 16 floats");
        for (const auto& parameter : json["parameters"].GetArray()) {
            const auto type = parameter.IsObject() ? string(parameter, "type") : std::string{};
            if (loaded.version == 3 && type == "bool") fields(parameter, {"id", "label", "type", "default"});
            else fields(parameter, {"id", "label", "type", "default", "min", "max"});
            const auto id = string(parameter, "id");
            if (!identifier(id) || loaded.parameters.contains(id) || (type != "float" && !(loaded.version == 3 && type == "bool")))
                throw std::runtime_error("Invalid, duplicate or unsupported parameter: " + id);
            if (type == "bool") {
                if (!parameter.HasMember("default") || !parameter["default"].IsBool()) throw std::runtime_error("Bool default required");
                loaded.parameters.registerBool(id, string(parameter, "label"), parameter["default"].GetBool()); continue;
            }
            const auto minimum = number(parameter, "min"), maximum = number(parameter, "max"), initial = number(parameter, "default");
            if (minimum > maximum || initial < minimum || initial > maximum) throw std::runtime_error("Invalid parameter range: " + id);
            loaded.parameters.registerFloat(id, string(parameter, "label"), initial, minimum, maximum);
        }
        if (json.HasMember("parameterSource")) {
            fields(json["parameterSource"], {"pluginId", "schemaVersion"});
            loaded.parameterSource = string(json["parameterSource"], "pluginId");
            const auto* schema = builtinRenderPluginParameterSchema(loaded.parameterSource);
            const auto& source = json["parameterSource"];
            if (!schema || !source.HasMember("schemaVersion") || !source["schemaVersion"].IsInt()
                || source["schemaVersion"].GetInt() != schema->version) throw std::runtime_error("Unknown/incompatible parameter source schema");
            for (const auto& parameter : loaded.parameters.descriptors()) {
                const auto* bound = schema->metadata.descriptor(parameter.id);
                if (!bound || bound->type != parameter.type || (parameter.type == ModuleParameterType::Float
                    && (bound->minimum < parameter.minimum || bound->maximum > parameter.maximum)))
                    throw std::runtime_error("Parameter source type/range mismatch: " + parameter.id);
            }
        }
        if (!shaderSceneTextureInputsCurrent(loaded)) throw std::runtime_error("Textures changed during package load");
        package = std::move(loaded); error.clear(); return true;
    } catch (const std::exception& exception) { error = "ShaderScene " + path.string() + ": " + exception.what(); return false; }
}
std::string shaderSceneResourceFor(const RendererSettings& settings) {
    if (!settings.shaderSceneResource.empty() && settings.enscapeCubeShaderEnabled)
        throw std::runtime_error("ShaderScene and Enscape modes are mutually exclusive");
    if (!settings.shaderSceneResource.empty()) return settings.shaderSceneResource;
    return settings.enscapeCubeShaderEnabled ? settings.enscapeShaderSceneResource : std::string{};
}
std::vector<ModuleParameterValue> shaderSceneParameterValues(const ShaderScenePackage& package, const RendererSettings& settings) {
    auto registry = package.parameters;
    if (package.parameterSource.empty()) {
        std::string error;
        std::unordered_set<std::string> ids;
        for (const auto& entry : settings.shaderSceneParameters) {
            const auto* descriptor = registry.descriptor(entry.id);
            if (!descriptor || !ids.insert(entry.id).second || entry.value.type != descriptor->type
                || (entry.value.type == ModuleParameterType::Float && (!std::isfinite(entry.value.number)
                    || entry.value.number < descriptor->minimum || entry.value.number > descriptor->maximum)))
                throw std::runtime_error("Invalid ShaderScene override: " + entry.id);
        }
        if (!registry.applyOverrides(settings.shaderSceneParameters, error)) throw std::runtime_error(error);
    } else if (!settings.shaderSceneParameters.empty()) {
        throw std::runtime_error("Bound ShaderScene parameters must use the CPU schema");
    }
    std::vector<ModuleParameterOverride> bound;
    if (!package.parameterSource.empty()) bound = builtinRenderPluginParameterSchema(package.parameterSource)->capture(settings);
    std::vector<ModuleParameterValue> result;
    for (const auto& descriptor : package.parameters.descriptors()) {
        auto value = *registry.value(descriptor.id);
        for (const auto& source : bound) if (source.id == descriptor.id) value = source.value;
        if (value.type != descriptor.type || (value.type == ModuleParameterType::Float
            && (!std::isfinite(value.number) || value.number < descriptor.minimum || value.number > descriptor.maximum)))
            throw std::runtime_error("Invalid ShaderScene parameter source value: " + descriptor.id);
        result.push_back(std::move(value));
    }
    return result;
}
bool setShaderSceneParameter(const ShaderScenePackage& package, RendererSettings& settings,
    const ModuleParameterOverride& parameter, bool& changed, std::string& error) {
    changed = false;
    try {
        auto candidate = settings;
        const auto before = shaderSceneParameterValues(package, settings);
        if (!package.parameterSource.empty()) {
            if (!package.parameters.contains(parameter.id)) throw std::runtime_error("Parameter is not public: " + parameter.id);
            bool history = false;
            if (!builtinRenderPluginParameterSchema(package.parameterSource)->apply(candidate, {parameter}, history, error))
                throw std::runtime_error(error);
        } else {
            auto& overrides = candidate.shaderSceneParameters;
            auto found = std::find_if(overrides.begin(), overrides.end(), [&](const auto& item) { return item.id == parameter.id; });
            if (found == overrides.end()) overrides.push_back(parameter); else *found = parameter;
            const auto values = shaderSceneParameterValues(package, candidate); // Strict rejection before canonicalization.
            auto registry = package.parameters;
            for (std::size_t i = 0; i < values.size(); ++i)
                if (!registry.setValue(package.parameters.descriptors()[i].id, values[i], error)) throw std::runtime_error(error);
            candidate.shaderSceneParameters = registry.overrides();
        }
        const auto after = shaderSceneParameterValues(package, candidate);
        for (std::size_t i = 0; i < before.size(); ++i)
            changed = changed || before[i].number != after[i].number || before[i].boolean != after[i].boolean;
        settings = std::move(candidate); error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
bool shaderScenePassNeedsHistory(const ShaderScenePackage& package, std::size_t index) {
    if (package.version < 3) return true; // Preserve existing v1/v2 allocation contracts.
    for (const auto& pass : package.passes) for (const auto& channel : pass.channels)
        if (!channel.externalTexture && channel.previousFrame && channel.sourcePass == index) return true;
    return false;
}
}
