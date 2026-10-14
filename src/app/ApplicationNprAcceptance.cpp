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

void Application::interactNprParameters(const std::vector<iris::PluginControlBounds>& controls) {
    if(!std::getenv("IRIS_NPR_UI_TEST") || ++nprUiFrames_<5 || nprUiPhase_>=3)return;
    auto& io=ImGui::GetIO();
    const char* parameterId=std::getenv("IRIS_NPR_SCREEN_TEST") ? "/stylizedDitherStrength" : "/stylizedRimIntensity";
    for(const auto& control:controls)if(control.id==std::string(iris::nprPluginId)+parameterId) {
        const auto pos=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
        if(nprUiPhase_==0) {
            if(control.y<pos.y+70 || control.y>pos.y+size.y-20){ImGui::SetScrollFromPosY(control.y-pos.y,.5f);return;}
            io.AddFocusEvent(true);io.AddMousePosEvent(control.x,control.y);io.AddMouseButtonEvent(0,true);
        }else if(nprUiPhase_==1)io.AddMousePosEvent(control.x+40,control.y);
        else io.AddMouseButtonEvent(0,false);
        ++nprUiPhase_;break;
    }
}

bool Application::nprRegression() {
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto directory=std::filesystem::u8path(std::getenv("IRIS_NPR_ACCEPTANCE_DIRECTORY"));
        std::filesystem::create_directories(directory);
        const bool available=iris::builtinRenderPlugins().contains(iris::nprPluginId);
        if(std::getenv("IRIS_NPR_UI_TEST"))check(nprUiPhase_==3 && (std::getenv("IRIS_NPR_SCREEN_TEST") ? rendererSettings_.stylizedDitherStrength : rendererSettings_.stylizedRimIntensity)!=nprUiInitialValue_,"Real NPR parameter drag did not apply");
        const auto authored=rendererSettings_;
        rendererSettings_.shaderHotReloadEnabled=false;rendererSettings_.temporalAaEnabled=false;rendererSettings_.water.timeSeconds=0;
        rendererSettings_.renderPath=RenderPath::Forward;rendererSettings_.shadingMode=ShadingMode::PhysicallyBased;
        const auto draw=[&](int w=640,int h=360){renderer_->render(scene_.buildRenderItems(),camera_,rendererSettings_,w,h);glFinish();check(glGetError()==GL_NO_ERROR,"NPR GL error");};
        const auto pixels=[&](){std::vector<unsigned char> data(640U*360U*4U);glBindTexture(GL_TEXTURE_2D,renderer_->colorTexture());glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,data.data());return data;};
        const auto command=[&](EditorCommand edit){editorSession_.request(std::move(edit));processEditorCommands();};
        const auto mode=[&](ShadingMode value){EditorCommand edit{EditorCommandType::SetShadingSettings};edit.shading=EditorDomain::captureShadingSettings(rendererSettings_);edit.shading.shadingMode=static_cast<int>(value);command(std::move(edit));};
        const auto enabled=[&](bool value){EditorCommand edit{EditorCommandType::SetRenderPluginEnabled};edit.text=iris::nprPluginId;edit.flag=value;command(std::move(edit));};
        const auto parameter=[&](float value){EditorCommand edit{EditorCommandType::SetRenderPluginParameter};edit.text=iris::nprPluginId;edit.pluginParameterId="stylizedRimIntensity";edit.moduleParameter.type=static_cast<int>(ModuleParameterType::Float);edit.moduleParameter.number=value;command(std::move(edit));};
        draw();const auto native=pixels();check(!renderer_->hasRenderPluginInstance(iris::nprPluginId),"PBR retained NPR instance");
        if(!available) {
            const auto history=renderer_->historyInvalidationRevision();mode(ShadingMode::Stylized);
            check(rendererSettings_.shadingMode==ShadingMode::PhysicallyBased && renderer_->historyInvalidationRevision()==history,"Missing NPR shading command committed");
            auto invalid=rendererSettings_;invalid.shadingMode=ShadingMode::Stylized;
            bool rejected=false;try{renderer_->render(scene_.buildRenderItems(),camera_,invalid,640,360);}catch(const std::exception& e){rejected=std::string(e.what()).find(iris::nprPluginId)!=std::string::npos;}
            check(rejected && renderer_->historyInvalidationRevision()==history && pixels()==native,"Missing NPR render changed history/pixels");
            std::cout<<"NPR OFF ordinary scene / shading command / render rejection preserved state PASS\n";
        }else {
            mode(ShadingMode::Stylized);draw();check(renderer_->hasRenderPluginInstance(iris::nprPluginId),"NPR instance not created");
            const auto initial=pixels();parameter(2.0f);draw();auto edited=pixels();
            check(initial!=edited && rendererSettings_.stylizedRimIntensity==2.0f && rendererSettings_.renderPluginValues.empty(),"NPR edit lost contribution or created parallel values");
            std::string error;
            if (std::getenv("IRIS_NPR_SCREEN_TEST")) {
                const auto editScreen=[&](const char* id, bool on) {
                    EditorCommand edit{EditorCommandType::SetRenderPluginParameter};
                    edit.text=iris::nprPluginId; edit.pluginParameterId=id;
                    edit.moduleParameter.type=static_cast<int>(ModuleParameterType::Bool); edit.moduleParameter.boolean=on;
                    command(std::move(edit));
                };
                const char* effects[]={"stylizedOutlineEnabled","stylizedDitherEnabled","stylizedHeightFogEnabled","stylizedColorGradingEnabled"};
                rendererSettings_.stylizedHeightFogDensity=.55f;
                rendererSettings_.stylizedColorGradingLut=StylizedColorGradingLut::NightAurora;
                for (auto path : {RenderPath::Forward,RenderPath::Deferred}) {
                    rendererSettings_.renderPath=path;
                    for (const char* effect : effects) editScreen(effect,false);
                    draw(); const auto without=pixels();
                    for (const char* effect : effects) {
                        const auto before=renderer_->historyInvalidationRevision(); editScreen(effect,true);
                        check(renderer_->historyInvalidationRevision()>before,"Screen edit did not invalidate history");
                        draw();const auto with=pixels(); check(with!=without,"Screen effect had no GPU contribution");
                        check(renderer_->saveScreenshot(directory/(std::string(effect)+"-"+std::to_string(static_cast<int>(path))+".png"),error),"Cannot save screen effect");
                        // Legacy field writes and the typed command consume the same value.
                        if (std::string(effect)=="stylizedDitherEnabled") rendererSettings_.stylizedDitherEnabled=false;
                        else if (std::string(effect)=="stylizedOutlineEnabled") rendererSettings_.stylizedOutlineEnabled=false;
                        else if (std::string(effect)=="stylizedHeightFogEnabled") rendererSettings_.stylizedHeightFogEnabled=false;
                        else rendererSettings_.stylizedColorGradingEnabled=false;
                        draw(); check(pixels()==without,"Screen legacy fields diverged from command");
                        editScreen(effect,true);draw();check(pixels()==with,"Screen reenable changed fixed pixels");
                        const auto retained=renderer_->historyInvalidationRevision();
                        EditorCommand bad{EditorCommandType::SetRenderPluginParameter};bad.text=iris::nprPluginId;
                        bad.pluginParameterId="stylizedDitherStrength";bad.moduleParameter.type=static_cast<int>(ModuleParameterType::Float);
                        bad.moduleParameter.number=std::numeric_limits<float>::quiet_NaN();command(std::move(bad));
                        draw();check(pixels()==with && renderer_->historyInvalidationRevision()==retained,"Invalid screen command changed pixels/history");
                        editScreen(effect,false);draw();check(pixels()==without,"Screen disable did not restore pixels");
                    }
                }
                rendererSettings_.renderPath=RenderPath::Forward;
                for (const char* effect : effects) editScreen(effect,true);
                draw(); edited=pixels();
                std::cout<<"NPR screen effects both paths / contribution / legacy parity / history / rejection PASS\n";
            }
            const auto history=renderer_->historyInvalidationRevision();parameter(std::numeric_limits<float>::quiet_NaN());enabled(false);draw();
            check(rendererSettings_.stylizedRimIntensity==2.0f && iris::pluginEnabled(rendererSettings_.renderPlugins,iris::nprPluginId)
                && renderer_->historyInvalidationRevision()==history && pixels()==edited,"Rejected NPR parameter/disable changed state");
            check(renderer_->saveScreenshot(directory/"edited.png",error),"Cannot save NPR output");
            const auto saved=directory/"saved.myscene";check(saveSceneTo(saved),"NPR Scene save failed");
            parameter(.1f);check(openScene(saved),"NPR Scene reopen failed");processEditorCommands();draw();
            check(rendererSettings_.stylizedRimIntensity==2.0f && pixels()==edited,"NPR Scene roundtrip changed pixels");
            rendererSettings_.renderPath=RenderPath::Deferred;draw();const auto deferred=pixels();
            rendererSettings_.stylizedRimIntensity=.1f;draw();rendererSettings_.stylizedRimIntensity=2.0f;draw();
            check(pixels()==deferred,"NPR generic/legacy Deferred values differ");
            draw(320,180);draw();check(pixels()==deferred,"NPR resize changed fixed output");
            check(renderer_->rebuildPluginResources(iris::nprPluginId,rendererSettings_),"NPR replacement rejected");draw();
            check(pixels()==deferred,"NPR replacement changed output");
            mode(ShadingMode::PhysicallyBased);draw();enabled(false);
            const auto before=renderer_->historyInvalidationRevision();mode(ShadingMode::Stylized);draw();
            check(rendererSettings_.shadingMode==ShadingMode::PhysicallyBased && renderer_->historyInvalidationRevision()==before
                && !renderer_->hasRenderPluginInstance(iris::nprPluginId),"Disabled NPR switch changed settings/history");
            SceneDocument bad;check(loadSceneDocument(saved,bad,error),"Cannot load NPR saved fixture");
            bad.renderer.renderPlugins={{iris::nprPluginId,false}};const auto invalid=directory/"disabled.myscene";
            check(saveSceneDocument(invalid,bad,error),"Cannot save disabled NPR fixture");
            const auto oldPath=currentScenePath_;check(!openScene(invalid) && oldPath==currentScenePath_ && before==renderer_->historyInvalidationRevision(),"Disabled NPR Scene replaced current Scene");
            enabled(true);mode(ShadingMode::Stylized);draw();check(renderer_->hasRenderPluginInstance(iris::nprPluginId),"NPR enable did not recreate instance");
            std::cout<<"NPR real parameter UI / both paths / legacy parity / history / failure retention / Scene / resize / replacement / disable-enable PASS\n";
        }
        rendererSettings_=authored;draw();return true;
    }catch(const std::exception& error){std::cerr<<"NPR acceptance failed: "<<error.what()<<'\n';return false;}
}
