#include "plugin/RenderPluginConfiguration.h"
#include "plugin/RenderPluginRegistry.h"
#include "render/Renderer.h"
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
    iris::RenderPluginRegistry shading;
    shading.add({iris::postProcessPluginId,iris::renderPluginApiVersion,{iris::openGlFullscreenService},"MIT"},factory);
    RendererSettings settings;
    require(iris::validateRendererPluginConfiguration(shading,settings,error),"Ordinary native scene requires NPR");
    settings.shadingMode=ShadingMode::Stylized;
    require(!iris::validateRendererPluginConfiguration(shading,settings,error) && error.find(iris::nprPluginId)!=std::string::npos,"Missing NPR accepted");
    shading.add({iris::nprPluginId,iris::renderPluginApiVersion,{iris::openGlShadingService},"MIT",{iris::PluginStage::MaterialShading,{},{}}},factory);
    require(iris::validateRendererPluginConfiguration(shading,settings,error),"NPR service rejected");
    settings.renderPlugins={{iris::postProcessPluginId,false}};
    require(!iris::validateRendererPluginConfiguration(shading,settings,error)
        && error.find(iris::postProcessPluginId)!=std::string::npos,"NPR screen effects accepted a disabled PostProcess executor");
    settings.renderPlugins={{iris::nprPluginId,false}};
    require(!iris::validateRendererPluginConfiguration(shading,settings,error),"Active disabled NPR accepted");
    settings.shadingMode=ShadingMode::PhysicallyBased;
    require(iris::validateRendererPluginConfiguration(shading,settings,error),"Inactive NPR cannot be disabled");
    settings.water.enabled=true;
    require(!iris::validateRendererPluginConfiguration(shading,settings,error)
        && error.find(iris::waterPluginId)!=std::string::npos,"Missing water capability accepted");
    shading.add({iris::waterPluginId,iris::renderPluginApiVersion,{iris::openGlShadingService,iris::openGlIndexedGeometryService},"MIT",{iris::PluginStage::MaterialShading,{},{}}},factory);
    require(iris::validateRendererPluginConfiguration(shading,settings,error),"Water shading service rejected");
    settings.renderPlugins={{iris::waterPluginId,false}};
    require(!iris::validateRendererPluginConfiguration(shading,settings,error),"Active disabled water accepted");
    settings.water.enabled=false;require(iris::validateRendererPluginConfiguration(shading,settings,error),"Dry scene requires water");
    settings.shadingMode=ShadingMode::Stylized;settings.shaderSceneResource="package.shaderscene";
    require(iris::validateRendererPluginConfiguration(shading,settings,error),"Fullscreen scene requires native NPR");
    settings.enscapeCubeShaderEnabled=true;
    require(!iris::validateRendererPluginConfiguration(shading,settings,error),"Conflicting pipeline configuration accepted");
    std::cout<<"Plugin activation dependency/transaction contracts: PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
