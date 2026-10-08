#include "app/RenderPluginPanel.h"
#include "app/EditorUi.h"
#include <iostream>
#include <stdexcept>
namespace {void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}}
int main(){
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(800,700);io.DeltaTime=1.f/60;
    unsigned char* fontPixels;int fontWidth,fontHeight;io.Fonts->GetTexDataAsRGBA32(&fontPixels,&fontWidth,&fontHeight);
    EditorUi::chinese=false;int result=0;
    try{
        iris::RenderPluginRegistry registry;iris::RenderPluginConfiguration config;std::string error;
        const std::vector<iris::RenderPluginDescriptor> catalog={
            {"optional",iris::renderPluginApiVersion,{},"MIT"},{"native",iris::renderPluginApiVersion,{},"MIT"},
            {"missing",iris::renderPluginApiVersion,{},"MIT"}};
        for(int i=0;i<2;++i)registry.add(catalog[i],[](const std::filesystem::path&)->std::unique_ptr<iris::RenderPlugin>{return {};});
        std::vector<iris::PluginControlBounds> controls;
        const auto frame=[&]()->std::optional<EditorCommand>{
            ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(10,10));ImGui::SetNextWindowSize(ImVec2(360,650));
            ImGui::Begin("Plugins",nullptr,ImGuiWindowFlags_NoSavedSettings);
            auto change=iris::drawRenderPluginPanel(registry,catalog,config,"native",&controls);
            ImGui::End();ImGui::Render();return change;
        };
        frame();frame();
        const auto click=[&](int index){const auto position=controls.at(index);
            io.AddMousePosEvent(position.x,position.y);io.AddMouseButtonEvent(0,true);frame();
            io.AddMouseButtonEvent(0,false);return frame();};
        auto command=click(0);require(command&&command->type==EditorCommandType::SetRenderPluginEnabled&&!command->flag,"Real checkbox click did not emit disable command");
        require(iris::setPluginEnabled(registry,config,"native",command->text,command->flag,error),"UI change failed");
        frame();command=click(0);require(command&&command->flag,"Reenable click failed");
        require(iris::setPluginEnabled(registry,config,"native",command->text,command->flag,error),"UI reenable rejected");
        frame();require(!click(1),"Required plugin checkbox was interactive");
        frame();require(!click(2),"Unavailable plugin checkbox was interactive");
        std::cout<<"Plugin GUI checkbox/locked/unavailable controls: PASS\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    ImGui::DestroyContext();return result;
}
