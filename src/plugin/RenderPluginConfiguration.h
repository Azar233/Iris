#pragma once
#include <string>
#include <vector>
#include <set>
#include <cctype>
namespace iris {
struct RenderPluginActivation { std::string id; bool enabled{true}; };
using RenderPluginConfiguration=std::vector<RenderPluginActivation>;
inline bool pluginEnabled(const RenderPluginConfiguration& config,const std::string& id){
    for(const auto& entry:config)if(entry.id==id)return entry.enabled;
    return true; // Old scenes default to every compiled capability enabled.
}
inline bool validPluginConfigurationShape(const RenderPluginConfiguration& config,std::string& error){
    if(config.size()>64){error="Too many render plugin entries";return false;}
    std::set<std::string> ids;
    for(const auto& entry:config){
        if(entry.id.empty()||entry.id.size()>128||!ids.insert(entry.id).second){error="Invalid/duplicate render plugin ID";return false;}
        for(unsigned char c:entry.id)if(!std::isalnum(c)&&c!='.'&&c!='_'&&c!='-'){error="Invalid render plugin ID";return false;}
    }
    return true;
}
class RenderPluginRegistry;
bool validatePluginConfiguration(const RenderPluginRegistry& registry,
    const RenderPluginConfiguration& config,const std::string& required,std::string& error);
bool setPluginEnabled(const RenderPluginRegistry& registry,RenderPluginConfiguration& config,
    const std::string& required,const std::string& id,bool enabled,std::string& error);
} // namespace iris
