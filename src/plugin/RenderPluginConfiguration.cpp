#include "plugin/RenderPluginConfiguration.h"
#include "plugin/RenderPluginRegistry.h"
#include "render/Renderer.h"
#include <algorithm>
namespace iris {
bool setPluginParameter(const RenderPluginRegistry& registry, RendererSettings& settings,
    const std::string& id, const ModuleParameterOverride& parameter, bool& affectsHistory, std::string& error) {
    affectsHistory = false;
    const auto* descriptor = registry.descriptor(id);
    if (!descriptor || !descriptor->parameters || !pluginEnabled(settings.renderPlugins, id)) {
        error = "Plugin parameters unavailable or plugin disabled: " + id;
        return false;
    }
    return descriptor->parameters->apply(settings, {parameter}, affectsHistory, error);
}
bool validatePluginConfiguration(const RenderPluginRegistry& registry,
    const RenderPluginConfiguration& config,const std::string& required,std::string& error){
    if(!validPluginConfigurationShape(config,error))return false;
    for(const auto& entry:config){
        if(!entry.enabled)continue;
        const auto* descriptor=registry.descriptor(entry.id);
        if(!descriptor){error="Render plugin unavailable: "+entry.id;return false;}
        for(const auto& service:descriptor->requiredServices)
            if(service!=openGlFullscreenService){error="Render plugin service unavailable: "+service;return false;}
    }
    if(!required.empty()&&(!registry.contains(required)||!pluginEnabled(config,required))){
        error="Current pipeline requires enabled render plugin: "+required;return false;
    }
    error.clear();return true;
}
bool setPluginEnabled(const RenderPluginRegistry& registry,RenderPluginConfiguration& config,
    const std::string& required,const std::string& id,bool enabled,std::string& error){
    if(!registry.contains(id)){error="Render plugin unavailable: "+id;return false;}
    auto candidate=config;
    auto entry=std::find_if(candidate.begin(),candidate.end(),[&](const auto& value){return value.id==id;});
    if(entry==candidate.end())candidate.push_back({id,enabled});else entry->enabled=enabled;
    if(!validatePluginConfiguration(registry,candidate,required,error))return false;
    config=std::move(candidate);return true;
}
} // namespace iris
