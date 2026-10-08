#include "plugin/RenderPluginConfiguration.h"
#include "plugin/RenderPluginRegistry.h"
#include <iostream>
#include <stdexcept>
namespace {void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}}
int main(){try{
    iris::RenderPluginRegistry registry;
    const auto factory=[](const std::filesystem::path&)->std::unique_ptr<iris::RenderPlugin>{return {};};
    registry.add({"native",iris::renderPluginApiVersion,{iris::openGlFullscreenService},"MIT"},factory);
    registry.add({"optional",iris::renderPluginApiVersion,{},"MIT"},factory);
    iris::RenderPluginConfiguration config;std::string error;
    require(iris::validatePluginConfiguration(registry,config,"native",error),"Legacy defaults rejected");
    require(iris::setPluginEnabled(registry,config,"native","optional",false,error),"Inactive plugin cannot disable");
    require(!iris::pluginEnabled(config,"optional"),"Disable did not apply");
    require(!iris::setPluginEnabled(registry,config,"native","native",false,error),"Required plugin disabled");
    require(config.size()==1&&!iris::pluginEnabled(config,"optional"),"Rejected change mutated configuration");
    require(!iris::setPluginEnabled(registry,config,"native","missing",true,error),"Missing plugin enabled");
    require(iris::setPluginEnabled(registry,config,"native","optional",true,error),"Inactive plugin cannot reenable");
    config.push_back({"future.uninstalled",false});
    require(iris::validatePluginConfiguration(registry,config,"native",error),"Disabled missing plugin was not portable");
    config.back().enabled=true;require(!iris::validatePluginConfiguration(registry,config,"native",error),"Missing active plugin accepted");
    config={{"optional",true},{"optional",false}};
    require(!iris::validatePluginConfiguration(registry,config,"native",error),"Duplicate plugin config accepted");
    std::cout<<"Plugin activation dependency/transaction contracts: PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
