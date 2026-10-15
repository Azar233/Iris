#include "app/Application.h"
#include "app/EditorDomain.h"
#include "app/RenderPluginPanel.h"
#include "plugin/RenderPluginRegistry.h"
#include <glad/gl.h>
#include <imgui.h>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <cmath>
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
            const auto matrixNear=[](const glm::mat4& a,const glm::mat4& b){
                for(int column=0;column<4;++column)for(int row=0;row<4;++row)
                    if(std::abs(a[column][row]-b[column][row])>1e-6f)return false;
                return true;
            };
            const auto frameSettings=rendererSettings_;
            const auto frameCamera=camera_.orbitState();
            // Drive the actual backend, including independent VP/time fallbacks.
            const std::array<float,5> deltas{std::nextafter(.2f,0.f),.2f,
                std::nextafter(.2f,1.f),-std::nextafter(.2f,0.f),-.2f};
            for(const auto path:{RenderPath::Forward,RenderPath::Deferred}) {
                rendererSettings_.renderPath=path;rendererSettings_.water.surfaceOptics=false;
                for(std::size_t sample=0;sample<deltas.size();++sample) {
                    rendererSettings_.temporalAaEnabled=false;rendererSettings_.water.timeSeconds=0;draw();
                    rendererSettings_.temporalAaEnabled=true;renderer_->invalidateTemporalHistory();draw();
                    const auto first=renderer_->renderedWaterFrameInputs();
                    auto projection=camera_.projectionMatrix(640.f/360.f);
                    projection[2][1]+=2.f*(1.f/3.f-.5f)/360.f;
                    check(matrixNear(first.currentViewProjection,projection*camera_.viewMatrix())
                        && matrixNear(first.previousViewProjection,first.currentViewProjection)
                        && first.previousTimeSeconds==0 && !first.motionHistoryValid,"First water frame lost jitter/fallback");
                    rendererSettings_.water.timeSeconds=deltas[sample];draw();
                    const auto second=renderer_->renderedWaterFrameInputs();
                    check(second.motionHistoryValid==(sample==0 || sample==3)
                        && second.previousTimeSeconds==0
                        && matrixNear(second.previousViewProjection,first.currentViewProjection),"Water time boundary or VP history changed");
                }
                rendererSettings_.water.timeSeconds=.125f;draw();
                renderer_->invalidateTemporalHistory();rendererSettings_.water.timeSeconds=.175f;draw();
                const auto reset=renderer_->renderedWaterFrameInputs();
                check(!reset.motionHistoryValid && reset.previousTimeSeconds==.125f
                    && matrixNear(reset.previousViewProjection,reset.currentViewProjection),"VP invalidation also discarded independent water time");
                rendererSettings_.water.enabled=false;draw();const auto dry=renderer_->renderedWaterFrameInputs();
                rendererSettings_.water.enabled=true;rendererSettings_.water.timeSeconds=.19f;draw();
                const auto enabled=renderer_->renderedWaterFrameInputs();
                check(!enabled.motionHistoryValid && enabled.previousTimeSeconds==.19f
                    && matrixNear(enabled.previousViewProjection,dry.currentViewProjection),"Water time fallback discarded valid camera VP");
                rendererSettings_.water.timeSeconds=.2f;draw(320,180);
                check(!renderer_->renderedWaterFrameInputs().motionHistoryValid,"Resize retained water motion history");
                draw();check(!renderer_->renderedWaterFrameInputs().motionHistoryValid,"Resize-back retained water motion history");
                rendererSettings_.water.surfaceOptics=true;rendererSettings_.water.level=0;
                camera_.setOrbitPose({0,3,0},45,0,6,45);draw();
                const auto transitions=renderer_->waterMediumTransitions();
                camera_.setOrbitPose({0,-3,0},45,0,6,45);draw();
                const auto medium=renderer_->renderedWaterFrameInputs();
                check(renderer_->waterMediumTransitions()==transitions+1 && !medium.motionHistoryValid
                    && medium.cameraSurfaceHeight==renderer_->cameraWaterSurfaceHeight(),"Water medium transition lost frame reset/height");
                camera_.setOrbitState(frameCamera);
            }
            // Same-instance transitions exercise the backend's current cloud
            // selection, including stale lastFrameActive/texture after clouds off.
            for(const auto path:{RenderPath::Forward,RenderPath::Deferred}) {
                rendererSettings_=frameSettings;rendererSettings_.renderPath=path;
                rendererSettings_.temporalAaEnabled=false;rendererSettings_.water.timeSeconds=0;
                rendererSettings_.water.surfaceOptics=true;rendererSettings_.water.cloudReflectionStrength=1;
                auto& atmosphere=rendererSettings_.atmosphere;
                atmosphere.enabled=true;atmosphere::applyCloudPreset(atmosphere,atmosphere::CloudPreset::Cumulus);
                atmosphere.cloudQuality=atmosphere::CloudQualityTier::Low;
                atmosphere.cloudHalfResolution=true;atmosphere.cloudTemporalEnabled=true;atmosphere.cloudsEnabled=false;
                camera_.setOrbitPose({0,1.6f,-30},-4,-3,16,58);
                renderer_->invalidateTemporalHistory();draw();const auto clear=pixels();
                const auto warm=[&]{renderer_->invalidateTemporalHistory();for(int i=0;i<16;++i)draw();};
                atmosphere.cloudsEnabled=true;warm();const auto resolved=pixels();
                check(resolved!=clear,"Cloud reflection transition fixture has no cloudy pixels");
                atmosphere.cloudsEnabled=false;draw();check(pixels()==clear,"Cloud disable retained previous reflection");
                atmosphere.cloudsEnabled=true;warm();check(pixels()==resolved,"Cloud reenable selected stale temporal texture");
                atmosphere.cloudTemporalEnabled=false;renderer_->invalidateTemporalHistory();draw();const auto raw=pixels();
                atmosphere.cloudTemporalEnabled=true;warm();check(pixels()==resolved,"Temporal reenable changed reflection selection");
                draw(641,361);warm();check(pixels()==resolved,"Odd resize retained a stale reflection texture");
                atmosphere.cloudTemporalEnabled=false;renderer_->invalidateTemporalHistory();draw();
                check(pixels()==raw,"Temporal disable retained resolved reflection");
            }
            rendererSettings_=frameSettings;camera_.setOrbitState(frameCamera);renderer_->invalidateTemporalHistory();draw();
            std::cout<<"Water same-instance cloud off/on/off/on / raw-resolved / odd resize reflection PASS\n";
            std::cout<<"Water resolved frame jitter / independent VP-time fallback / +/-0.2 boundary / resize / medium PASS\n";
            std::cout<<"Water owned grid release/restart: "<<waterBytes<<" bytes PASS\n";
            std::cout<<"Water real parameter UI / both paths / legacy parity / history / rejection / Scene / resize / replacement / disable-enable PASS\n";
        }
        rendererSettings_=authored;draw();return true;
    }catch(const std::exception& error){std::cerr<<"Water acceptance failed: "<<error.what()<<'\n';return false;}
}
