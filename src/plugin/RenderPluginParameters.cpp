#include "plugin/RenderPluginParameters.h"
#include "plugin/RenderPlugin.h"
#include "render/Renderer.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <limits>
#include <stdexcept>

namespace iris {
namespace {
const RenderPluginParameterBinding* bindingFor(const RenderPluginParameterSchema& schema, const std::string& id) {
    for (const auto& binding : schema.bindings) if (binding.id == id) return &binding;
    return nullptr;
}
bool validValue(const ModuleParameterDescriptor& descriptor, const ModuleParameterValue& value) {
    if (descriptor.type != value.type) return false;
    const auto range = [&](double n) { return std::isfinite(n) && n >= descriptor.minimum && n <= descriptor.maximum; };
    switch (value.type) {
        case ModuleParameterType::Bool: return true;
        case ModuleParameterType::Int: return range(value.integer);
        case ModuleParameterType::Float: return range(value.number);
        case ModuleParameterType::Color: return range(value.color.x) && range(value.color.y) && range(value.color.z);
        case ModuleParameterType::Enum: return value.integer >= 0
            && static_cast<std::size_t>(value.integer) < descriptor.enumLabels.size()
            && (value.text.empty() || value.text == descriptor.enumLabels[static_cast<std::size_t>(value.integer)]);
        case ModuleParameterType::Asset: return false;
    }
    return false;
}
}
void RenderPluginParameterSchema::validate() const {
    std::string error;
    RenderPluginConfiguration identity;
    identity.push_back({pluginId, true});
    if (!validPluginConfigurationShape(identity, error) || version < 1 || metadata.size() > 64U)
        throw std::invalid_argument("Invalid plugin parameter schema: " + pluginId);
    std::set<std::string> ids;
    for (const auto& descriptor : metadata.descriptors()) {
        if (descriptor.id.empty() || descriptor.id.size() > 128U
            || !ids.insert(descriptor.id).second || !std::isfinite(descriptor.minimum)
            || !std::isfinite(descriptor.maximum) || descriptor.minimum > descriptor.maximum
            || !validValue(descriptor, *metadata.value(descriptor.id)))
            throw std::invalid_argument("Invalid plugin parameter metadata: " + descriptor.id);
        if (descriptor.type == ModuleParameterType::Enum) {
            std::set<std::string> labels;
            for (const auto& label : descriptor.enumLabels)
                if (label.empty() || !labels.insert(label).second)
                    throw std::invalid_argument("Invalid plugin Enum labels: " + descriptor.id);
        }
    }
    ids.clear();
    for (const auto& binding : bindings)
        if (!metadata.descriptor(binding.id) || !ids.insert(binding.id).second || !binding.read || !binding.write)
            throw std::invalid_argument("Invalid plugin parameter binding: " + binding.id);
}
std::vector<ModuleParameterOverride> RenderPluginParameterSchema::capture(const RendererSettings& settings) const {
    auto values = metadata;
    values.resetToDefaults();
    bool found = false;
    for (const auto& stored : settings.renderPluginValues) {
        if (stored.pluginId != pluginId) continue;
        if (found || stored.schemaVersion != version || stored.values.size() > 64U)
            throw std::runtime_error("Invalid stored plugin schema: " + pluginId);
        found = true;
        std::set<std::string> ids;
        for (const auto& entry : stored.values) {
            const auto* descriptor = metadata.descriptor(entry.id);
            std::string error;
            if (!descriptor || bindingFor(*this, entry.id) || !ids.insert(entry.id).second
                || !validValue(*descriptor, entry.value) || !values.setValue(entry.id, entry.value, error))
                throw std::runtime_error("Invalid stored plugin parameter: " + pluginId + "/" + entry.id);
        }
    }
    std::vector<ModuleParameterOverride> result;
    for (const auto& descriptor : metadata.descriptors()) {
        const auto* binding = bindingFor(*this, descriptor.id);
        result.push_back({descriptor.id, binding ? binding->read(settings) : *values.value(descriptor.id)});
    }
    return result;
}

bool RenderPluginParameterSchema::apply(RendererSettings& settings,
    const std::vector<ModuleParameterOverride>& entries, bool& affectsHistory, std::string& error) const {
    affectsHistory = false;
    try {
        if (entries.size() > 64U) throw std::runtime_error("Too many plugin parameter values");
        auto validated = metadata;
        validated.resetToDefaults();
        // Preserve previous unbound values across partial edits.
        for (const auto& entry : capture(settings))
            if (!bindingFor(*this, entry.id) && !validated.setValue(entry.id, entry.value, error))
                throw std::runtime_error(error);
        std::set<std::string> ids;
        bool history = false;
        for (const auto& entry : entries) {
            const auto* descriptor = metadata.descriptor(entry.id);
            if (!descriptor || !ids.insert(entry.id).second || !validValue(*descriptor, entry.value))
                throw std::runtime_error("Unknown, duplicate, mistyped or out-of-range plugin parameter: " + pluginId + "/" + entry.id);
            if (!validated.setValue(entry.id, entry.value, error)) throw std::runtime_error(error);
            const auto* binding = bindingFor(*this, entry.id);
            history = history || (binding ? binding->affectsHistory : storedValuesAffectHistory);
        }
        auto candidate = settings;
        for (const auto& entry : entries)
            if (const auto* binding = bindingFor(*this, entry.id))
                binding->write(candidate, *validated.value(entry.id));
        std::vector<ModuleParameterOverride> stored;
        for (const auto& descriptor : metadata.descriptors())
            if (!bindingFor(*this, descriptor.id) && !validated.isDefault(descriptor.id))
                stored.push_back({descriptor.id, *validated.value(descriptor.id)});
        auto& store = candidate.renderPluginValues;
        store.erase(std::remove_if(store.begin(), store.end(), [&](const auto& item) { return item.pluginId == pluginId; }), store.end());
        if (!stored.empty()) {
            if (store.size() >= 64U) throw std::runtime_error("Too many stored plugin schemas");
            store.push_back({pluginId, version, std::move(stored)});
            std::sort(store.begin(), store.end(), [](const auto& a, const auto& b) { return a.pluginId < b.pluginId; });
        }
        settings = std::move(candidate);
        affectsHistory = history;
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

void RenderPluginParameterCatalog::add(RenderPluginParameterSchema schema) {
    schema.validate();
    if (schemas_.size() >= 64U || find(schema.pluginId))
        throw std::invalid_argument("Duplicate or excessive plugin schema: " + schema.pluginId);
    const auto id = schema.pluginId;
    schemas_.emplace(id, std::move(schema));
}
const RenderPluginParameterSchema* RenderPluginParameterCatalog::find(const std::string& id) const {
    const auto entry = schemas_.find(id);
    return entry == schemas_.end() ? nullptr : &entry->second;
}
bool RenderPluginParameterCatalog::validateSettings(const RendererSettings& settings, std::string& error) const {
    if (settings.renderPluginValues.size() > 64U) { error = "Too many stored plugin schemas"; return false; }
    std::set<std::string> ids;
    for (const auto& entry : settings.renderPluginValues)
        if (!find(entry.pluginId) || !ids.insert(entry.pluginId).second) {
            error = "Unknown or duplicate stored plugin schema: " + entry.pluginId; return false;
        }
    auto candidate = settings;
    for (const auto& entry : schemas_) {
        bool history = false;
        try {
            if (!entry.second.apply(candidate, entry.second.capture(settings), history, error)) return false;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    error.clear();
    return true;
}
const RenderPluginParameterCatalog& builtinRenderPluginParameterCatalog() {
    static const auto catalog = [] {
        RenderPluginParameterCatalog result;
        for (const auto& schema : builtinRenderPluginParameterSchemas()) result.add(schema);
        return result;
    }();
    return catalog;
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
