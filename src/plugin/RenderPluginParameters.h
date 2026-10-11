#pragma once

#include <functional>
#include <map>
#include <filesystem>
#include <string>
#include <vector>
#include "module/ParameterRegistry.h"

struct RendererSettings;

namespace iris {

// Optional bindings keep existing renderer controls compatible. Unbound metadata
// is owned by the scene's plugin value store, with defaults from the schema.
struct RenderPluginParameterBinding {
    std::string id;
    std::function<ModuleParameterValue(const RendererSettings&)> read;
    std::function<void(RendererSettings&, const ModuleParameterValue&)> write;
    bool affectsHistory{true};
};

struct RenderPluginParameterSchema {
    std::string pluginId;
    int version{1};
    ParameterRegistry metadata;
    std::vector<RenderPluginParameterBinding> bindings;
    bool storedValuesAffectHistory{true};
    // Explicit CPU migration to this schema's current version. Input values are
    // staged; invalid migrated output is rejected by the normal strict validator.
    std::function<bool(int, std::vector<ModuleParameterOverride>&, std::string&)> migrate;

    void validate() const;

    std::vector<ModuleParameterOverride> capture(const RendererSettings& settings) const;
    // Unlike module numeric clamping, edits and serialized plugin values must
    // satisfy the declared range. All writes commit together or leave settings intact.
    bool apply(RendererSettings& settings, const std::vector<ModuleParameterOverride>& values,
        bool& affectsHistory, std::string& error) const;
    bool importValues(RendererSettings& settings, int sourceVersion,
        const std::vector<ModuleParameterOverride>& values, bool& affectsHistory, std::string& error,
        const std::filesystem::path& resourceRoot = {}) const;
};

// CPU catalog owns schemas independently of compiled GPU factories. Registered
// schemas remain available for scene validation even when a plugin is not built.
class RenderPluginParameterCatalog {
public:
    void add(RenderPluginParameterSchema schema);
    const RenderPluginParameterSchema* find(const std::string& id) const;
    const std::map<std::string, RenderPluginParameterSchema>& schemas() const { return schemas_; }
    bool validateSettings(const RendererSettings& settings, std::string& error) const;
private:
    std::map<std::string, RenderPluginParameterSchema> schemas_;
};
const RenderPluginParameterCatalog& builtinRenderPluginParameterCatalog();

// CPU-only schemas are available even in builds without the GPU implementation.
const std::vector<RenderPluginParameterSchema>& builtinRenderPluginParameterSchemas();
const RenderPluginParameterSchema* builtinRenderPluginParameterSchema(const std::string& id);
} // namespace iris
