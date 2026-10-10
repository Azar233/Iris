#pragma once

#include <functional>
#include <string>
#include <vector>
#include "module/ParameterRegistry.h"

struct RendererSettings;

namespace iris {

// Metadata and bindings use the existing parameter vocabulary. Values remain in
// the shared RendererSettings bridge; neither the UI nor schema owns a second copy.
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

    void validate() const;

    std::vector<ModuleParameterOverride> capture(const RendererSettings& settings) const;
    // Unlike module numeric clamping, edits and serialized plugin values must
    // satisfy the declared range. All writes commit together or leave settings intact.
    bool apply(RendererSettings& settings, const std::vector<ModuleParameterOverride>& values,
        bool& affectsHistory, std::string& error) const;
};

// CPU-only schemas are available even in builds without the GPU implementation.
const std::vector<RenderPluginParameterSchema>& builtinRenderPluginParameterSchemas();
const RenderPluginParameterSchema* builtinRenderPluginParameterSchema(const std::string& id);
} // namespace iris
