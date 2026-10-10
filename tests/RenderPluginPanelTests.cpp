#include "app/RenderPluginPanel.h"
#include "app/EditorUi.h"
#include "render/Renderer.h"
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
        // Descriptor-provided metadata produces actual widgets and the same command
        // used by the application, including edits in the currently required plugin.
        auto schema = *iris::builtinRenderPluginParameterSchema(iris::postProcessPluginId);
        schema.pluginId = "native";
        schema.metadata.clear();
        schema.metadata.registerBool("toneMapping", "Tone mapping with a deliberately long label for minimum-width layout", true);
        schema.metadata.registerBool("bloom", "Bloom", true);
        schema.metadata.registerFloat("exposure", "Exposure", 1, 0, 4);
        schema.metadata.registerFloat("bloomThreshold", "Bloom threshold", 1, 0, 4);
        schema.metadata.registerFloat("bloomIntensity", "Bloom intensity", 0.12f, 0, 1);
        auto parameterCatalog = catalog;
        parameterCatalog[1].parameters = &schema;
        iris::RenderPluginRegistry parameterRegistry;
        for (int i = 0; i < 2; ++i) parameterRegistry.add(parameterCatalog[i],
            [](const std::filesystem::path&) -> std::unique_ptr<iris::RenderPlugin> { return {}; });
        RendererSettings settings;
        std::vector<iris::PluginControlBounds> parameters;
        const auto parameterFrame = [&]() -> std::optional<EditorCommand> {
            ImGui::NewFrame(); ImGui::SetNextWindowPos(ImVec2(10,10)); ImGui::SetNextWindowSize(ImVec2(260,650));
            ImGui::Begin("Parameters",nullptr,ImGuiWindowFlags_NoSavedSettings);
            auto change = iris::drawRenderPluginPanel(parameterRegistry, parameterCatalog, settings.renderPlugins,
                "native", nullptr, &settings, &parameters);
            ImGui::End(); ImGui::Render(); return change;
        };
        parameterFrame(); parameterFrame();
        require(parameters.size() == 5U, "Schema did not generate five postprocess controls");
        const auto position = parameters.front();
        io.AddMousePosEvent(position.x, position.y); io.AddMouseButtonEvent(0,true); parameterFrame();
        io.AddMouseButtonEvent(0,false); command = parameterFrame();
        require(command && command->type == EditorCommandType::SetRenderPluginParameter
            && command->text == "native" && command->pluginParameterId == "toneMapping"
            && !command->moduleParameter.boolean, "Real generated parameter click did not emit typed command");
        ModuleParameterOverride edit; edit.id = command->pluginParameterId;
        edit.value.type = static_cast<ModuleParameterType>(command->moduleParameter.type);
        edit.value.boolean = command->moduleParameter.boolean;
        bool history = false;
        require(iris::setPluginParameter(parameterRegistry, settings, "native", edit, history, error)
            && !settings.toneMapping && !history, "Generated control did not edit shared settings");
        parameterFrame();
        const auto floatControl = parameters[2];
        io.AddMousePosEvent(floatControl.x, floatControl.y); io.AddMouseButtonEvent(0,true); parameterFrame();
        io.AddMousePosEvent(floatControl.x+40.0f, floatControl.y); command = parameterFrame();
        require(command && command->pluginParameterId == "exposure" && command->moduleParameter.number > 1.0f,
            "Real generated Float drag did not emit a value change");
        io.AddMouseButtonEvent(0,false); parameterFrame();
        // Not-built parameters remain visible but disabled, including their label.
        parameterCatalog[1].parameters = &schema;
        iris::RenderPluginRegistry missingRegistry;
        const auto unavailableFrame = [&]() -> std::optional<EditorCommand> {
            ImGui::NewFrame(); ImGui::SetNextWindowPos(ImVec2(10,10)); ImGui::SetNextWindowSize(ImVec2(260,650));
            ImGui::Begin("Parameters",nullptr,ImGuiWindowFlags_NoSavedSettings);
            auto change = iris::drawRenderPluginPanel(missingRegistry, parameterCatalog, settings.renderPlugins,
                "native", nullptr, &settings, &parameters);
            ImGui::End(); ImGui::Render(); return change;
        };
        unavailableFrame();
        const auto unavailable = parameters.front();
        io.AddMousePosEvent(unavailable.x, unavailable.y); io.AddMouseButtonEvent(0,true); unavailableFrame();
        io.AddMouseButtonEvent(0,false); require(!unavailableFrame(), "Unavailable plugin parameter was interactive");
        std::cout<<"Plugin GUI checkbox/locked/unavailable controls: PASS\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    ImGui::DestroyContext();return result;
}
