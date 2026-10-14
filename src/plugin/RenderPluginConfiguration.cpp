#include "plugin/RenderPluginConfiguration.h"
#include "plugin/RenderPluginRegistry.h"
#include "render/Renderer.h"
#include "plugin/ShaderScene.h"
#include <algorithm>
namespace iris {
bool nprRequired(const RendererSettings& settings) {
    return settings.shadingMode==ShadingMode::Stylized && !settings.enscapeCubeShaderEnabled
        && shaderSceneResourceFor(settings).empty();
}
bool waterRequired(const RendererSettings& settings) {
    return settings.water.enabled && !settings.enscapeCubeShaderEnabled && shaderSceneResourceFor(settings).empty();
}
bool validateRendererPluginConfiguration(const RenderPluginRegistry& registry,
    const RendererSettings& settings, std::string& error) {
    if(!settings.shaderSceneResource.empty() && settings.enscapeCubeShaderEnabled) {
        error="ShaderScene and Enscape modes are mutually exclusive";return false;
    }
    const std::string required=!shaderSceneResourceFor(settings).empty()?"":settings.enscapeCubeShaderEnabled?enscapePluginId:postProcessPluginId;
    if(!validatePluginConfiguration(registry,settings.renderPlugins,required,error))return false;
    if(nprRequired(settings) && (!registry.contains(nprPluginId) || !pluginEnabled(settings.renderPlugins,nprPluginId))) {
        error="Current pipeline requires enabled render plugin: "+std::string(nprPluginId);return false;
    }
    if(waterRequired(settings) && (!registry.contains(waterPluginId) || !pluginEnabled(settings.renderPlugins,waterPluginId))) {
        error="Current pipeline requires enabled render plugin: "+std::string(waterPluginId);return false;
    }
    error.clear();return true;
}
bool setPluginParameter(const RenderPluginRegistry& registry, RendererSettings& settings,
    const std::string& id, const ModuleParameterOverride& parameter, bool& affectsHistory, std::string& error) {
    affectsHistory = false;
    const auto* descriptor = registry.descriptor(id);
    if (!descriptor || !descriptor->parameters || !pluginEnabled(settings.renderPlugins, id)) {
        error = "Plugin parameters unavailable or plugin disabled: " + id;
        return false;
    }
    if (!descriptor->parameters->apply(settings, {parameter}, affectsHistory, error)) return false;
    if (id==waterPluginId && (parameter.id=="amplitude" || parameter.id=="speed" || parameter.id=="steepness"
        || parameter.id=="waveDiversity" || parameter.id=="nearMeshFocus" || parameter.id=="windX" || parameter.id=="windZ"))
        settings.water.preset=WaterPreset::Custom;
    return true;
}
bool validatePluginConfiguration(const RenderPluginRegistry& registry,
    const RenderPluginConfiguration& config,const std::string& required,std::string& error){
    if(!validPluginConfigurationShape(config,error))return false;
    for(const auto& entry:config){
        if(!entry.enabled)continue;
        const auto* descriptor=registry.descriptor(entry.id);
        if(!descriptor){error="Render plugin unavailable: "+entry.id;return false;}
        for(const auto& service:descriptor->requiredServices)
            if(service!=openGlFullscreenService && service!=openGlShadingService && service!=openGlIndexedGeometryService && service!=openGlShaderBorrowService){error="Render plugin service unavailable: "+service;return false;}
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
