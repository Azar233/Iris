#include "app/Application.h"
#include "app/EditorDomain.h"
#include "app/RenderPluginPanel.h"
#include "plugin/RenderPluginRegistry.h"
#include <glad/gl.h>
#include <imgui.h>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
void Application::interactWaterParameters(const std::vector<iris::PluginControlBounds>& controls) {
    if(!std::getenv("IRIS_WATER_UI_TEST") || ++waterUiFrames_<5 || waterUiPhase_>=3)return;
    auto& io=ImGui::GetIO();
    for(const auto& control:controls)if(control.id==std::string(iris::waterPluginId)+"/amplitude") {
        const auto pos=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
        if(waterUiPhase_==0) {
            if(control.y<pos.y+70 || control.y>pos.y+size.y-20){ImGui::SetScrollFromPosY(control.y-pos.y,.5f);return;}
            io.AddFocusEvent(true);io.AddMousePosEvent(control.x,control.y);io.AddMouseButtonEvent(0,true);
        }else if(waterUiPhase_==1)io.AddMousePosEvent(control.x+40,control.y);
        else io.AddMouseButtonEvent(0,false);
        ++waterUiPhase_;break;
    }
}
bool Application::waterPluginRegression() {
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto directory=std::filesystem::u8path(std::getenv("IRIS_WATER_ACCEPTANCE_DIRECTORY"));
        std::filesystem::create_directories(directory);
        const bool available=iris::builtinRenderPlugins().contains(iris::waterPluginId);
        if(std::getenv("IRIS_WATER_UI_TEST"))check(waterUiPhase_==3 && rendererSettings_.water.amplitude!=waterUiInitialAmplitude_,"Real water parameter drag did not apply");
        const auto authored=rendererSettings_;
        rendererSettings_.shaderHotReloadEnabled=false;rendererSettings_.temporalAaEnabled=false;rendererSettings_.water.timeSeconds=0;
        rendererSettings_.renderPath=RenderPath::Forward;rendererSettings_.water.enabled=false;
        const auto draw=[&](int w=640,int h=360){renderer_->render(scene_.buildRenderItems(),camera_,rendererSettings_,w,h);glFinish();check(glGetError()==GL_NO_ERROR,"Water GL error");};
        const auto pixels=[&](){std::vector<unsigned char> data(640U*360U*4U);glBindTexture(GL_TEXTURE_2D,renderer_->colorTexture());glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,data.data());return data;};
        const auto command=[&](EditorCommand edit){editorSession_.request(std::move(edit));processEditorCommands();};
        const auto legacyEnabled=[&](bool value){EditorCommand edit{EditorCommandType::SetWaterSettings};edit.water=EditorDomain::captureWaterSettings(rendererSettings_);edit.water.enabled=value;command(std::move(edit));};
        const auto activation=[&](bool value){EditorCommand edit{EditorCommandType::SetRenderPluginEnabled};edit.text=iris::waterPluginId;edit.flag=value;command(std::move(edit));};
        const auto parameter=[&](float value){EditorCommand edit{EditorCommandType::SetRenderPluginParameter};edit.text=iris::waterPluginId;edit.pluginParameterId="amplitude";edit.moduleParameter.type=static_cast<int>(ModuleParameterType::Float);edit.moduleParameter.number=value;command(std::move(edit));};
        draw();const auto native=pixels();check(!renderer_->hasRenderPluginInstance(iris::waterPluginId),"Dry scene retained water instance");
        if(!available) {
            const auto history=renderer_->historyInvalidationRevision();legacyEnabled(true);parameter(.8f);
            check(!rendererSettings_.water.enabled && renderer_->historyInvalidationRevision()==history,"Missing water command committed");
            auto invalid=rendererSettings_;invalid.water.enabled=true;
            bool rejected=false;try{renderer_->render(scene_.buildRenderItems(),camera_,invalid,640,360);}catch(const std::exception& e){rejected=std::string(e.what()).find(iris::waterPluginId)!=std::string::npos;}
            check(rejected && renderer_->historyInvalidationRevision()==history && pixels()==native,"Missing water render changed state");
            std::cout<<"Water OFF ordinary scene / commands / render rejection preserved state PASS\n";
        }else {
            legacyEnabled(true);draw();check(renderer_->hasRenderPluginInstance(iris::waterPluginId),"Water instance not created");
            const auto initial=pixels();parameter(.8f);draw();const auto edited=pixels();
            check(initial!=edited && rendererSettings_.water.amplitude==.8f && rendererSettings_.water.preset==WaterPreset::Custom
                && rendererSettings_.renderPluginValues.empty(),"Water command lost contribution/preset or created parallel values");
            const auto history=renderer_->historyInvalidationRevision();parameter(std::numeric_limits<float>::quiet_NaN());activation(false);draw();
            check(rendererSettings_.water.amplitude==.8f && iris::pluginEnabled(rendererSettings_.renderPlugins,iris::waterPluginId)
                && renderer_->historyInvalidationRevision()==history && pixels()==edited,"Rejected water edit changed state");
            std::string error;check(renderer_->saveScreenshot(directory/"edited.png",error),"Cannot save water output");
            const auto saved=directory/"saved.myscene";check(saveSceneTo(saved),"Water Scene save failed");
            parameter(.1f);check(openScene(saved),"Water Scene reopen failed");processEditorCommands();draw();check(pixels()==edited,"Water roundtrip changed output");
            rendererSettings_.renderPath=RenderPath::Deferred;draw();const auto deferred=pixels();
            rendererSettings_.water.amplitude=.1f;draw();parameter(.8f);draw();check(pixels()==deferred,"Water legacy/generic values differ");
            draw(320,180);draw();check(pixels()==deferred,"Water resize changed output");
            check(renderer_->rebuildPluginResources(iris::waterPluginId,rendererSettings_),"Water replacement rejected");draw();check(pixels()==deferred,"Water replacement changed pixels");
            const auto activeMemory=renderer_->estimatedRenderMemoryBytes();
            const auto gridBytes=[](int resolution){return static_cast<std::size_t>((resolution+1)*(resolution+1)*8+resolution*resolution*24);};
            const auto waterBytes=gridBytes(water::gridResolution)+gridBytes(water::lowGridResolution);
            legacyEnabled(false);draw();
            check(!renderer_->hasRenderPluginInstance(iris::waterPluginId)
                && activeMemory-renderer_->estimatedRenderMemoryBytes()==waterBytes,"Water disable did not release grid memory");
            activation(false);const auto before=renderer_->historyInvalidationRevision();legacyEnabled(true);draw();
            check(!rendererSettings_.water.enabled && renderer_->historyInvalidationRevision()==before && !renderer_->hasRenderPluginInstance(iris::waterPluginId),"Disabled water switch changed state");
            SceneDocument bad;check(loadSceneDocument(saved,bad,error),"Cannot load water saved fixture");bad.renderer.renderPlugins={{iris::waterPluginId,false}};
            const auto invalid=directory/"disabled.myscene";check(saveSceneDocument(invalid,bad,error),"Cannot save disabled water fixture");
            const auto oldPath=currentScenePath_;check(!openScene(invalid) && currentScenePath_==oldPath && before==renderer_->historyInvalidationRevision(),"Disabled water Scene replaced current scene");
            activation(true);legacyEnabled(true);draw();check(renderer_->hasRenderPluginInstance(iris::waterPluginId),"Water did not recreate instance");
            check(renderer_->estimatedRenderMemoryBytes()==activeMemory,"Water restart memory differs");
            std::cout<<"Water owned grid release/restart: "<<waterBytes<<" bytes PASS\n";
            std::cout<<"Water real parameter UI / both paths / legacy parity / history / rejection / Scene / resize / replacement / disable-enable PASS\n";
        }
        rendererSettings_=authored;draw();return true;
    }catch(const std::exception& error){std::cerr<<"Water acceptance failed: "<<error.what()<<'\n';return false;}
}
