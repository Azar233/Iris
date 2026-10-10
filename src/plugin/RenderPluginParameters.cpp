#include "plugin/RenderPluginParameters.h"
#include "plugin/RenderPlugin.h"
#include "render/Renderer.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <limits>
#include <stdexcept>

namespace iris {
void RenderPluginParameterSchema::validate() const {
    if (pluginId.empty() || version < 1 || bindings.size() != metadata.size() || bindings.size() > 64U)
        throw std::invalid_argument("Invalid plugin parameter schema: " + pluginId);
    std::set<std::string> ids;
    for (const auto& binding : bindings) {
        const auto* descriptor = metadata.descriptor(binding.id);
        if (!descriptor || binding.id.empty() || binding.id.size() > 128U
            || !ids.insert(binding.id).second || !binding.read || !binding.write
            || !std::isfinite(descriptor->minimum) || !std::isfinite(descriptor->maximum)
            || descriptor->minimum > descriptor->maximum)
            throw std::invalid_argument("Invalid plugin parameter binding: " + binding.id);
    }
}
std::vector<ModuleParameterOverride> RenderPluginParameterSchema::capture(const RendererSettings& settings) const {
    std::vector<ModuleParameterOverride> result;
    for (const auto& binding : bindings) result.push_back({binding.id, binding.read(settings)});
    return result;
}

bool RenderPluginParameterSchema::apply(RendererSettings& settings,
    const std::vector<ModuleParameterOverride>& values, bool& affectsHistory, std::string& error) const {
    affectsHistory = false;
    auto validated = metadata;
    std::set<std::string> ids;
    bool history = false;
    for (const auto& entry : values) {
        const auto* descriptor = metadata.descriptor(entry.id);
        const auto binding = std::find_if(bindings.begin(), bindings.end(), [&](const auto& b) { return b.id == entry.id; });
        if (!descriptor || binding == bindings.end() || !ids.insert(entry.id).second
            || descriptor->type != entry.value.type) {
            error = "Unknown, duplicate or mistyped plugin parameter: " + pluginId + "/" + entry.id;
            return false;
        }
        const auto inRange = [&](double number) {
            return std::isfinite(number) && number >= descriptor->minimum && number <= descriptor->maximum;
        };
        const auto& value = entry.value;
        if ((value.type == ModuleParameterType::Float && !inRange(value.number))
            || (value.type == ModuleParameterType::Int && !inRange(value.integer))
            || (value.type == ModuleParameterType::Color
                && (!inRange(value.color.x) || !inRange(value.color.y) || !inRange(value.color.z)))
            || (value.type == ModuleParameterType::Enum
                && (value.integer < 0 || static_cast<std::size_t>(value.integer) >= descriptor->enumLabels.size()
                    || (!value.text.empty() && descriptor->enumLabels[static_cast<std::size_t>(value.integer)] != value.text)))
            || value.type == ModuleParameterType::Asset) {
            error = "Plugin parameter out of range or unsupported resource: " + pluginId + "/" + entry.id;
            return false;
        }
        if (!validated.setValue(entry.id, value, error)) return false;
        history = history || binding->affectsHistory;
    }
    auto candidate = settings;
    try {
        for (const auto& entry : values) {
            const auto binding = std::find_if(bindings.begin(), bindings.end(), [&](const auto& b) { return b.id == entry.id; });
            binding->write(candidate, *validated.value(entry.id));
        }
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    settings = std::move(candidate);
    affectsHistory = history;
    error.clear();
    return true;
}

namespace {
void addBool(RenderPluginParameterSchema& schema, const char* id, const char* label,
    std::function<bool(const RendererSettings&)> read, std::function<void(RendererSettings&, bool)> write,
    bool history) {
    schema.metadata.registerBool(id, label, read(RendererSettings{}));
    schema.bindings.push_back({id, [read](const RendererSettings& s) {
        ModuleParameterValue value; value.type = ModuleParameterType::Bool; value.boolean = read(s); return value;
    }, [write](RendererSettings& s, const ModuleParameterValue& v) { write(s, v.boolean); }, history});
}
void addFloat(RenderPluginParameterSchema& schema, const char* id, const char* label, float minimum, float maximum,
    std::function<float(const RendererSettings&)> read, std::function<void(RendererSettings&, float)> write,
    bool history) {
    schema.metadata.registerFloat(id, label, read(RendererSettings{}), minimum, maximum);
    schema.bindings.push_back({id, [read](const RendererSettings& s) {
        ModuleParameterValue value; value.type = ModuleParameterType::Float; value.number = read(s); return value;
    }, [write](RendererSettings& s, const ModuleParameterValue& v) { write(s, v.number); }, history});
}
}

const std::vector<RenderPluginParameterSchema>& builtinRenderPluginParameterSchemas() {
    static const auto schemas = [] {
        RenderPluginParameterSchema post;
        post.pluginId = postProcessPluginId;
#define POST_BOOL(field, label) addBool(post, #field, label, [](const auto& s) { return s.field; }, [](auto& s, bool v) { s.field = v; }, false)
#define POST_FLOAT(field, label, min, max) addFloat(post, #field, label, min, max, [](const auto& s) { return s.field; }, [](auto& s, float v) { s.field = v; }, false)
        POST_BOOL(toneMapping, "Tone mapping");
        POST_BOOL(bloom, "Bloom");
        // Legacy postprocess values were not range-clamped on Scene load. Keep
        // their finite range portable; UI uses compact drag controls for this bridge.
        POST_FLOAT(exposure, "Exposure", std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
        POST_FLOAT(bloomThreshold, "Bloom threshold", std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
        POST_FLOAT(bloomIntensity, "Bloom intensity", std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
#undef POST_BOOL
#undef POST_FLOAT
        RenderPluginParameterSchema ocean;
        ocean.pluginId = enscapePluginId;
#define OCEAN_BOOL(field, label) addBool(ocean, #field, label, [](const auto& s) { return s.enscapeCube.field; }, [](auto& s, bool v) { s.enscapeCube.field = v; }, true)
#define OCEAN_FLOAT(field, label, min, max) addFloat(ocean, #field, label, min, max, [](const auto& s) { return s.enscapeCube.field; }, [](auto& s, float v) { s.enscapeCube.field = v; }, true)
        OCEAN_BOOL(cubeEnabled, "Study cube");
        OCEAN_BOOL(noiseReduction, "Stable reflections");
        OCEAN_FLOAT(waveHeight, "Wave height", 0.05f, 1.5f);
        OCEAN_FLOAT(waveFrequency, "Wave frequency", 0.04f, 0.5f);
        OCEAN_FLOAT(waveChoppiness, "Wave choppiness", 1.0f, 8.0f);
        OCEAN_FLOAT(waveSpeed, "Wave speed", 0.0f, 2.0f);
        OCEAN_FLOAT(cloudCoverage, "Cloud coverage", 0.0f, 2.0f);
        OCEAN_FLOAT(reflectionStrength, "Reflection strength", 0.0f, 2.0f);
        OCEAN_FLOAT(underwaterClarity, "Underwater clarity", 0.25f, 3.0f);
        OCEAN_FLOAT(sunAzimuthDegrees, "Sun azimuth", -180.0f, 180.0f);
        OCEAN_FLOAT(sunElevationDegrees, "Sun elevation", 1.0f, 85.0f);
        OCEAN_FLOAT(bloomStrength, "Bloom strength", 0.0f, 3.0f);
        OCEAN_FLOAT(exposure, "Exposure", 0.25f, 3.0f);
#undef OCEAN_BOOL
#undef OCEAN_FLOAT
        post.validate(); ocean.validate();
        return std::vector<RenderPluginParameterSchema>{std::move(post), std::move(ocean)};
    }();
    return schemas;
}

const RenderPluginParameterSchema* builtinRenderPluginParameterSchema(const std::string& id) {
    for (const auto& schema : builtinRenderPluginParameterSchemas()) if (schema.pluginId == id) return &schema;
    return nullptr;
}
} // namespace iris
