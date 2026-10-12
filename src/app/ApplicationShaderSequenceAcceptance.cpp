#include "app/Application.h"
#include "runtime/ShaderSceneSequence.h"
#include "plugin/ShaderScene.h"
#include <glad/gl.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>

void Application::runShaderSequenceAcceptance(const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto bytes=[](const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),{});};
    std::filesystem::create_directories(directory);
    if(!openScene(sourceRoot_/"assets/shader-scenes/sequence/sequence.shaderscene")) throw std::runtime_error(statusMessage_);
    processEditorCommands();
    rendererSettings_.shaderHotReloadEnabled=false;
    check(saveSceneTo(directory/"saved.myscene"),"Sequence Scene save failed");
    RenderJob job;job.renderer="raster";job.shaderSceneSequence=true;job.shaderSceneWarmupFrames=3;
    job.startFrame=3;job.endFrame=8;job.framesPerSecond=24;
    job.scenePath=directory/"saved.myscene";job.renderSettings.width=320;job.renderSettings.height=180;
    job.renderSettings.seed=7;job.outputs={pathtracer::RenderOutput::Beauty};
    job.outputFormats={pathtracer::RenderFileFormat::Png};job.outputStemPattern=directory/"queued/frame_{frame:04}";
    const auto jobPath=directory/"sequence.renderjob";
    std::ofstream(jobPath,std::ios::binary) << "{\"format\":\"MyRendererRenderJob\",\"schemaVersion\":4,\"renderer\":\"raster\",\"scene\":\"saved.myscene\","
        "\"camera\":\"scene\",\"resolution\":[320,180],\"frames\":{\"start\":3,\"end\":8,\"fps\":24},"
        "\"sampling\":{\"spp\":1,\"maxDepth\":1,\"seed\":7},\"aovs\":[\"beauty\"],"
        "\"raster\":{\"determinism\":true,\"warmupFrames\":0,\"temporalAccumulation\":false},"
        "\"shaderScene\":{\"version\":1,\"history\":\"reset-at-start\",\"warmupFrames\":3},"
        "\"output\":{\"path\":\"queued/frame_{frame:04}\",\"formats\":[\"png\"],\"resume\":false},\"failurePolicy\":\"stop\"}";
    std::string error;ShaderSceneSequencePlan plan;
    check(makeShaderSceneSequencePlan(job,rendererSettings_.shaderSceneHistory,plan,error),error.c_str());
    std::filesystem::create_directories(directory/"preview");
    rendererSettings_.shaderSceneSeed=7;renderer_->invalidateTemporalHistory();
    for(int frame=plan.firstFrame;frame<=plan.lastFrame;++frame){
        rendererSettings_.water.timeSeconds=plan.timeSeconds(frame);
        renderer_->render(scene_.buildRenderItems(),camera_,rendererSettings_,320,180);glFinish();
        check(glGetError()==GL_NO_ERROR,"Sequence preview GL error");
        check(renderer_->shaderSceneRuntimeStatus().frame==plan.sampleIndex(frame),"Preview feedback reset");
        if(frame>=job.startFrame)check(renderer_->saveScreenshot(directory/"preview"/("frame_"+std::to_string(frame)+".png"),error),error.c_str());
    }
    // Submit through the shared editor command, then wait without touching GL.
    EditorCommand command{EditorCommandType::SubmitRenderJob};command.text=jobPath.u8string();
    editorSession_.request(command);processEditorCommands();
    check(!renderQueue_->entries().empty(),"Shared Queue submission rejected");
    const auto id=renderQueue_->entries().back().id;
    auto terminal=RenderQueueStatus::Pending;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    do {
        renderQueue_->update();
        for(const auto& entry:renderQueue_->entries())if(entry.id==id)terminal=entry.status;
        if(terminal==RenderQueueStatus::Complete||terminal==RenderQueueStatus::Failed||terminal==RenderQueueStatus::Cancelled)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }while(std::chrono::steady_clock::now()<deadline);
    check(terminal==RenderQueueStatus::Complete,"Raster Queue child failed/timed out");
    for(int frame=job.startFrame;frame<=job.endFrame;++frame){
        const auto preview=bytes(directory/"preview"/("frame_"+std::to_string(frame)+".png"));
        check(!preview.empty()&&preview==bytes(renderJobFrameStem(job,frame).string()+".png"),"GUI/Queue PNG mismatch");
    }
    // A changed seed is a history input, even under preserve-on-parameter policy.
    rendererSettings_.shaderSceneHistory.resetOnParameters=false;
    rendererSettings_.shaderSceneSeed=8;
    renderer_->render(scene_.buildRenderItems(),camera_,rendererSettings_,320,180);
    check(renderer_->shaderSceneRuntimeStatus().frame==0,"Changed sequence seed retained stale feedback");
    rendererSettings_.shaderSceneSeed=7;rendererSettings_.shaderSceneHistory.resetOnParameters=true;
    auto cancelText=bytes(jobPath);
    cancelText.replace(cancelText.find("\"end\":8"),7,"\"end\":999");
    cancelText.replace(cancelText.find("queued/frame"),12,"cancelled/frame");
    const auto cancelJob=directory/"cancel.renderjob";
    std::ofstream(cancelJob,std::ios::binary)<<cancelText;
    command.text=cancelJob.u8string();editorSession_.request(command);processEditorCommands();
    const auto cancelId=renderQueue_->entries().back().id;
    bool requested=false;
    const auto cancelDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    terminal=RenderQueueStatus::Pending;
    do {
        renderQueue_->update();
        for(const auto& entry:renderQueue_->entries())if(entry.id==cancelId){
            terminal=entry.status;
            if(!requested&&entry.completedFrames>=2){
                EditorCommand cancel{EditorCommandType::CancelRenderJob};cancel.entity=cancelId;
                editorSession_.request(cancel);processEditorCommands();requested=true;
            }
        }
        if(terminal==RenderQueueStatus::Cancelled||terminal==RenderQueueStatus::Failed||terminal==RenderQueueStatus::Complete)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }while(std::chrono::steady_clock::now()<cancelDeadline);
    check(requested&&terminal==RenderQueueStatus::Cancelled,"Raster Queue did not acknowledge cancellation");
    for(const auto& file:std::filesystem::directory_iterator(directory/"cancelled"))
        check(file.path().filename().string().find("partial")==std::string::npos,"Cancellation leaked staging output");
    // Leave the captured workspace displaying the same final frame as the Job.
    renderer_->invalidateTemporalHistory();
    for(int frame=plan.firstFrame;frame<=plan.lastFrame;++frame){
        rendererSettings_.water.timeSeconds=plan.timeSeconds(frame);
        renderer_->render(scene_.buildRenderItems(),camera_,rendererSettings_,320,180);
    }
    focusRenderQueueTab_=true;animationTimeFixed_=true;animationTimeSeconds_=plan.timeSeconds(job.endFrame);
    editorSession_.setFrameRange(job.startFrame,job.endFrame);editorSession_.setFramesPerSecond(job.framesPerSecond);
    editorSession_.setFrame(job.endFrame);
    std::cout<<"ShaderScene Timeline / GUI replay / Raster Queue six-frame PNG parity / safe cancellation: PASS\n";
}
