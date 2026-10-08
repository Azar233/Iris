#pragma once
#include <optional>
#include "app/EditorSession.h"
#include "plugin/RenderPluginRegistry.h"
#include "plugin/RenderPluginConfiguration.h"
namespace iris {
struct PluginControlBounds {std::string id;float x;float y;};
std::optional<EditorCommand> drawRenderPluginPanel(const RenderPluginRegistry& registry,
    const std::vector<RenderPluginDescriptor>& catalog,const RenderPluginConfiguration& config,
    const std::string& required,std::vector<PluginControlBounds>* controls=nullptr);
}
