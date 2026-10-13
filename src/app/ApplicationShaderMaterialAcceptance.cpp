#include "app/Application.h"
#include "render/GpuModel.h"
#include "module/BuiltinModules.h"
#include <glad/gl.h>
#include <imgui.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

// Runs on the application's GL thread, against imported geometry and the real Forward pipeline.
void Application::runShaderMaterialAcceptance(const std::filesystem::path& directory) {
    const auto check=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);};
    std::filesystem::create_directories(directory);
    const auto source=sourceRoot_/"assets/shader-materials/checker";
    for(const auto& file:std::filesystem::directory_iterator(source))
        if(file.is_regular_file())std::filesystem::copy_file(file.path(),directory/file.path().filename(),std::filesystem::copy_options::overwrite_existing);
    SceneDocument document;std::string error;
    check(loadSceneDocument(sourceRoot_/"assets/scenes/fixtures/29_shader_material.myscene",document,error),error);
    for(auto& entity:document.entities)entity.modelResource=resolveSceneResource(entity.modelResource,sourceRoot_/"assets/scenes/fixtures/29_shader_material.myscene").u8string();
    document.entities.front().shaderMaterial.resource=(directory/"checker.shadermaterial").u8string();
    document.renderer.shaderHotReloadEnabled=false;
    const auto scenePath=directory/"material.myscene";
    check(saveSceneDocument(scenePath,document,error),error);
    check(openScene(scenePath),"Material Scene did not open");processEditorCommands();
    const auto draw=[&](bool reload=false){
        auto settings=rendererSettings_;settings.shaderHotReloadEnabled=reload;settings.water.timeSeconds=0;
        renderer_->render(scene_.buildRenderItems(),camera_,settings,640,360);glFinish();
        check(glGetError()==GL_NO_ERROR,"Material render GL error");
    };
    const auto pixels=[&](){std::vector<unsigned char> bytes(640U*360U*4U);glBindTexture(GL_TEXTURE_2D,renderer_->colorTexture());
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());return bytes;};
    const auto save=[&](const char* name){check(renderer_->saveScreenshot(directory/name,error),error);};
    const auto id=document.entities.front().id;
    const auto update=[&](iris::ShaderMaterialBinding binding,bool retry=false){EditorCommand command{EditorCommandType::SetEntityShaderMaterial,id};
        command.shaderMaterial=std::move(binding);command.flag=retry;editorSession_.request(std::move(command));processEditorCommands();};
    draw();const auto initial=pixels();save("initial.png");
    scene_.find(id)->instanceCandidate=true;rendererSettings_.instanceOptimizationEnabled=true;
    draw();check(pixels()==initial,"Instancing omitted or changed custom material geometry");
    scene_.find(id)->instanceCandidate=false;rendererSettings_.instanceOptimizationEnabled=false;
    auto binding=scene_.find(id)->shaderMaterial;
    ModuleParameterOverride gain;gain.id="gain";gain.value.type=ModuleParameterType::Float;gain.value.number=.2f;binding.parameters={gain};
    update(binding);draw();const auto edited=pixels();check(edited!=initial,"Public parameter did not change model pixels");save("parameter.png");
    check(saveSceneTo(scenePath)&&openScene(scenePath),"Material save/reopen failed");processEditorCommands();draw();
    check(pixels()==edited,"Material roundtrip changed fixed pixels");
    const auto duplicate=scene_.duplicateEntity(id);check(duplicate!=invalidSceneEntityId,"Material duplicate failed");
    check(scene_.find(duplicate)->shaderMaterial.resource==binding.resource
        && scene_.find(duplicate)->shaderMaterial.parameters[0].value.number==.2f,"Duplicate lost material binding");
    scene_.destroyEntity(duplicate);
    const auto revision=renderer_->historyInvalidationRevision();
    binding.parameters[0].value.number=2;update(binding);draw();
    check(pixels()==edited&&renderer_->historyInvalidationRevision()==revision,"Invalid parameter changed state/history");
    EditorCommand cpu{EditorCommandType::BackendChanged};cpu.value=static_cast<std::uint64_t>(EditorRenderBackend::CpuPathTraced);
    editorSession_.request(cpu);processEditorCommands();processEditorCommands();
    check(viewportRenderMode_==0,"CPU backend accepted ShaderMaterial");
    EditorCommand module{EditorCommandType::SetActiveModule};module.text=BuiltinModules::turntableId;
    editorSession_.request(module);processEditorCommands();check(activeModuleId_.empty(),"Module accepted ShaderMaterial");
    auto unsupported=rendererSettings_;unsupported.renderPath=RenderPath::Deferred;
    bool rejected=false;try{renderer_->render(scene_.buildRenderItems(),camera_,unsupported,640,360);}catch(const std::exception&){rejected=true;}
    check(rejected&&renderer_->historyInvalidationRevision()==revision,"Deferred rejected after changing history");
    {std::ofstream(directory/"surface.frag")<<"#version 330 core\nthis is not GLSL\n";}
    check(!openScene(scenePath)&&renderer_->historyInvalidationRevision()==revision,"Bad Scene compile replaced working scene/history");
    draw();check(pixels()==edited,"Bad Scene compile lost previous program");
    for(int i=0;i<16;++i)draw(true);
    check(pixels()==edited&&!renderer_->shaderMaterialError().empty(),"Bad hot reload lost pixels/diagnostic");save("failed-retained.png");
    std::filesystem::copy_file(source/"surface.frag",directory/"surface.frag",std::filesystem::copy_options::overwrite_existing);
    for(int i=0;i<16;++i)draw(true);
    check(pixels()==edited&&renderer_->shaderMaterialError().empty(),"Shader repair did not recover");
    const auto read=[](const std::filesystem::path& path){std::ifstream in(path);return std::string(std::istreambuf_iterator<char>(in),{});};
    const auto validFragment=read(source/"surface.frag");auto wrongType=validFragment;
    wrongType.replace(wrongType.find("uniform float gain"),18,"uniform vec3 gain");
    const auto retainedHistory=renderer_->historyInvalidationRevision();
    {std::ofstream(directory/"surface.frag")<<wrongType;}
    for(int i=0;i<16;++i)draw(true);
    check(pixels()==edited&&!renderer_->shaderMaterialError().empty()
        &&renderer_->historyInvalidationRevision()==retainedHistory,"Uniform ABI rejection changed pixels/history");
    {std::ofstream(directory/"surface.frag")<<validFragment;}
    for(int i=0;i<16;++i)draw(true);
    const auto validManifest=read(source/"checker.shadermaterial");auto badRange=validManifest;
    badRange.replace(badRange.find("\"max\": 1"),8,"\"max\": 0.1");
    {std::ofstream(directory/"checker.shadermaterial")<<badRange;}
    const auto rangeHistory=renderer_->historyInvalidationRevision();
    for(int i=0;i<16;++i)draw(true);
    check(pixels()==edited&&!renderer_->shaderMaterialError().empty()
        &&renderer_->historyInvalidationRevision()==rangeHistory,"Changed descriptor range published over old parameters");
    {std::ofstream(directory/"checker.shadermaterial")<<validManifest;}
    for(int i=0;i<16;++i)draw(true);
    check(pixels()==edited&&renderer_->shaderMaterialError().empty(),"Descriptor repair did not recover");
    update({});draw();check(pixels()!=edited,"Clearing ShaderMaterial did not restore native material");save("native.png");
    update(document.entities.front().shaderMaterial,true);draw();check(pixels()==initial,"Rebinding did not restore defaults");
    // Imported texture and unsupported import classes use the same GpuModel contract.
    for(const auto* asset:{"alpha_material_test.gltf","glass_material_test.gltf","skinning_test.gltf"}) {
        auto candidate=document;candidate.entities.front().modelResource=(sourceRoot_/"assets/models"/asset).u8string();
        const auto rejectedPath=directory/"unsupported.myscene";
        check(saveSceneDocument(rejectedPath,candidate,error),error);
        const auto before=renderer_->historyInvalidationRevision();
        check(!openScene(rejectedPath)&&renderer_->historyInvalidationRevision()==before,"Unsupported model replaced Scene");
        draw();check(pixels()==initial,"Unsupported model changed prior output");
    }
    auto textured=document;textured.entities.front().modelResource=(sourceRoot_/"assets/models/pbr_material_test.gltf").u8string();
    check(saveSceneDocument(directory/"textured.myscene",textured,error),error);
    check(openScene(directory/"textured.myscene"),"Opaque textured import rejected");processEditorCommands();draw();
    check(scene_.find(id)->model->textureCount()>0,"Textured acceptance imported no textures");save("textured.png");
    check(openScene(scenePath),"Could not restore material Scene");processEditorCommands();
    const auto old=scene_.find(id)->shaderMaterial;
    const auto noopRevision=renderer_->historyInvalidationRevision();update(old);
    check(noopRevision==renderer_->historyInvalidationRevision(),"No-op material edit reset history");
    update(document.entities.front().shaderMaterial,true);draw();
    selectedWorkspaceAsset_.clear();selectEntity(id);focusObjectTab_=true;
    if (std::getenv("IRIS_SHADER_MATERIAL_RELOAD_UI_BAD")) {
        {std::ofstream(directory/"surface.frag")<<"#version 330 core\ninvalid material source\n";}
        for(int i=0;i<16;++i)draw(true);
        check(!renderer_->shaderMaterialError().empty(),"Failure UI fixture did not retain a diagnostic");
    }
    rendererSettings_.shaderHotReloadEnabled=false;animationEnabled_=false;
    std::cout<<"ShaderMaterial real Forward / parameters / Scene roundtrip / duplication / clear / compile transaction / reload repair / unsupported model+CPU+Deferred PASS\n";
}

void Application::runShaderMaterialWorkspaceAcceptance(const std::filesystem::path& directory) {
    runShaderMaterialAcceptance(directory);
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto shader=directory/"surface.frag";
    std::ifstream in(shader);std::string source(std::istreambuf_iterator<char>(in),{});
    source.insert(source.find('\n')+1,"#include \"workspace.glsl\"\n");
    std::ofstream(shader)<<source;
    std::ofstream(directory/"workspace.glsl")<<"// valid include\nfloat workspaceValue(){return .7;}\n";
    auto settings=rendererSettings_;settings.shaderHotReloadEnabled=false;settings.water.timeSeconds=0;
    renderer_->render(scene_.buildRenderItems(),camera_,settings,640,360);glFinish();
    std::string error;check(renderer_->saveScreenshot(directory/"expected.png",error),"Cannot save expected material output");
    EditorCommand clear{EditorCommandType::SetEntityShaderMaterial,selectedSceneEntity_};editorSession_.request(clear);processEditorCommands();
    const auto before=renderer_->historyInvalidationRevision();
    const auto count=scene_.entities().size();const auto scenePath=currentScenePath_;
    EditorCommand inspect{EditorCommandType::OpenSceneAsset};inspect.text=(directory/"checker.shadermaterial").u8string();
    editorSession_.request(inspect);processEditorCommands();
    check(scene_.entities().size()==count && currentScenePath_==scenePath && !scene_.hasShaderMaterials()
        && before==renderer_->historyInvalidationRevision(),"Inspecting a material package replaced Scene or history");
    check(workspaceAssets_.find(directory/"workspace.glsl") && inspectWorkspaceShaderPackage(directory/"checker.shadermaterial").modelMaterial,
        "Material dependencies were not registered");
    shaderMaterialWorkspaceDirectory_=directory;shaderMaterialWorkspaceEntityCount_=count;
    shaderWorkspaceCompileAttempts_=0;shaderWorkspaceLastCompileAccepted_=false;
    if(std::getenv("IRIS_SHADER_MATERIAL_WORKSPACE_NO_TARGET")) {
        selectEntity(invalidSceneEntityId);shaderMaterialWorkspacePhase_=13;
    }
    animationTimeSeconds_=0;animationTimeFixed_=true;animationPlaying_=false;animationEnabled_=false;
    std::cout<<"Model package inspection preserved Scene/history and registered include inputs PASS\n";
}
void Application::interactShaderMaterialWorkspace(int phase,int frame) {
    if(shaderMaterialWorkspaceDirectory_.empty() || shaderBrowserUiFrames_<frame) return;
    auto& io=ImGui::GetIO();
    if(shaderMaterialWorkspacePhase_==phase) {
        if(!ImGui::IsItemVisible()){ImGui::SetScrollHereY(.5f);return;}
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
        io.AddFocusEvent(true);io.AddMousePosEvent((a.x+b.x)*.5f,(a.y+b.y)*.5f);io.AddMouseButtonEvent(0,true);++shaderMaterialWorkspacePhase_;
    } else if(shaderMaterialWorkspacePhase_==phase+1){io.AddMouseButtonEvent(0,false);++shaderMaterialWorkspacePhase_;}
}
bool Application::finishShaderMaterialWorkspaceAcceptance() {
    if(shaderMaterialWorkspaceDirectory_.empty())return true;
    bool passed=shaderMaterialWorkspacePhase_==12 && shaderWorkspaceCompileAttempts_==3 && scene_.hasShaderMaterials()
        && scene_.entities().size()==shaderMaterialWorkspaceEntityCount_ && shaderWorkspaceDiagnostics_.empty();
    if(shaderMaterialWorkspacePhase_==13)passed=selectedSceneEntity_==invalidSceneEntityId && !scene_.hasShaderMaterials()
        && shaderWorkspaceCompileAttempts_==0 && scene_.entities().size()==shaderMaterialWorkspaceEntityCount_;
    if(passed) {
        auto settings=rendererSettings_;settings.shaderHotReloadEnabled=false;settings.water.timeSeconds=0;
        renderer_->render(scene_.buildRenderItems(),camera_,settings,640,360);glFinish();
        std::string error;passed=glGetError()==GL_NO_ERROR && renderer_->saveScreenshot(shaderMaterialWorkspaceDirectory_/"result.png",error);
    }
    std::cout<<"Material workspace real bind/recompile/include navigation/repair: "<<(passed?"PASS":"FAIL")
        <<" phase="<<shaderMaterialWorkspacePhase_<<" compiles="<<shaderWorkspaceCompileAttempts_<<'\n';return passed;
}
