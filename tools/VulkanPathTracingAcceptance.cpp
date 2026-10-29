#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "vulkan/VulkanPathTracer.h"
#include "asset/BuiltinModels.h"
#include "io/ObjLoader.h"
#include "io/AssimpImporter.h"
#include "render/Camera.h"
#include "pathtracer/SceneSnapshotCapture.h"
#include "runtime/BatchRuntime.h"
#include "scene/SceneDocument.h"
#include "runtime/RenderQueue.h"
#include "app/RasterSequenceProcess.h"
#include <fstream>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <type_traits>
#include <limits>
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
double error(const pathtracer::RenderImage& a,const pathtracer::RenderImage& b){
    const auto x=a.linearPixels(),y=b.linearPixels();double sum=0;
    for(std::size_t i=0;i<x.size();++i){const auto d=x[i]-y[i];sum+=glm::dot(d,d);}return std::sqrt(sum/(3*x.size()));
}
void queueAcceptance(const std::filesystem::path& out,const std::filesystem::path& scenePath) {
        const auto queueDirectory=out/"queue";std::filesystem::create_directories(queueDirectory);
        const auto writeJob=[&](const std::string& name,unsigned spp){
            const auto path=queueDirectory/(name+".renderjob");std::ofstream file(path);
            file<<"{\"format\":\"MyRendererRenderJob\",\"schemaVersion\":4,\"scene\":\""<<scenePath.generic_string()
                <<"\",\"renderer\":\"gpu-path-traced\",\"resolution\":[64,36],\"frames\":{\"start\":0,\"end\":1,\"fps\":30},\"sampling\":{\"spp\":"<<spp<<",\"maxDepth\":4,\"seed\":1},\"aovs\":[\"beauty\",\"normal\"],\"output\":{\"path\":\""
                <<(queueDirectory/name/"frame_{frame:04}").generic_string()<<"\",\"formats\":[\"png\",\"exr\"],\"resume\":false}}";file.close();return path;
        };
        const auto a=writeJob("a",2),b=writeJob("b",2),c=writeJob("cancel",65536);
        const auto state=queueDirectory/"state.json";std::string message;std::uint64_t ia=0,ib=0,ic=0;
        {RenderQueue queue(state);queue.setGpuRunner(runRasterSequenceProcess);
            require(queue.enqueue(a,ia,message),message.c_str());require(queue.enqueue(b,ib,message),message.c_str());
            require(queue.movePending(ib,-1,message) && queue.entries().front().id==ib,"GPU Queue reorder failed");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(50);
            while(std::chrono::steady_clock::now()<deadline){queue.update();const auto entries=queue.entries();if(entries[1].status==RenderQueueStatus::Complete)break;require(entries[0].status!=RenderQueueStatus::Failed && entries[1].status!=RenderQueueStatus::Failed,entries[0].message.c_str());std::this_thread::sleep_for(std::chrono::milliseconds(20));}
            for(const auto& entry:queue.entries())require(entry.status==RenderQueueStatus::Complete && entry.completedFrames==2 && entry.outputCount==10,"GPU Queue publication incomplete");
            require(queue.enqueue(c,ic,message),message.c_str());queue.update();std::this_thread::sleep_for(std::chrono::seconds(3));require(queue.cancel(ic,message),message.c_str());
            while(queue.hasActiveJob()){queue.update();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
            require(queue.entries().back().status==RenderQueueStatus::Cancelled && !std::filesystem::exists(queueDirectory/"cancel"),"GPU Queue cancellation published outputs");
            writeJob("cancel",2);require(queue.retry(ic,message),message.c_str());
            while(queue.entries().back().status==RenderQueueStatus::Pending || queue.hasActiveJob()){queue.update();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
            require(queue.entries().back().status==RenderQueueStatus::Complete,"GPU Queue retry failed");queue.shutdown();
        }
        {RenderQueue restored(state);require(restored.restore(message),message.c_str());for(const auto& entry:restored.entries())require(entry.status==RenderQueueStatus::Complete,"GPU Queue restore lost completion");}
        RenderJob changed;require(loadRenderJob(a,changed,message),message.c_str());changed.loadedSourceFingerprint="bad";
        require(runRasterSequenceProcess(changed,nullptr,{}).status==BatchFrameStatus::Failed,"Queue accepted stale Job identity");
        std::cout<<"GPU Queue PASS: real IrisBatch child, reorder, two-frame AOVs, cooperative cancel, retry, persistence, stale-input refusal\n";
    }
int lifecycle(const std::filesystem::path& root,const std::filesystem::path& out,bool indexed=false) {
    SceneDocument doc;std::string message;
    const auto scenePath=root/(indexed?"assets/benchmarks/m3c/sponza-atrium.myscene":"assets/benchmarks/m3c/stanford-glass.myscene");
    require(loadSceneDocument(scenePath,doc,message),message.c_str());
    std::vector<std::shared_ptr<ModelData>> models;AssimpImporter importer;
    for(const auto& e:doc.entities){
        require(e.parent==invalidSceneEntityId,"Lifecycle input must have flat hierarchy");
        if(e.modelResource=="builtin:ground-plane")models.push_back(std::make_shared<ModelData>(makeGroundPlaneData()));
        else models.push_back(std::make_shared<ModelData>(importer.load(scenePath.parent_path()/e.modelResource).model));
    }
    pathtracer::RenderSettings settings;settings.width=96;settings.height=54;settings.samplesPerPixel=4;settings.maxDepth=8;settings.seed=20261026;
    auto snapshot=[&](float yaw,float x){
        Camera camera;auto orbit=doc.camera;orbit.yawDegrees+=yaw;camera.setOrbitState(orbit);
        pathtracer::SnapshotCamera c;c.position=camera.position();c.view=camera.viewMatrix();c.aspectRatio=static_cast<float>(settings.width)/settings.height;c.projection=camera.projectionMatrix(c.aspectRatio);c.verticalFieldOfViewRadians=glm::radians(camera.fieldOfView());
        auto lights=doc.renderer;lights.localLights=resolveSceneLocalLights(doc);
        pathtracer::SceneSnapshotBuilder b(c,pathtracer::captureSceneLighting(lights,c.position.y));
        for(std::size_t i=0;i<models.size();++i){auto transform=doc.entities[i].transform.matrix();if(i==0)transform=glm::translate(glm::mat4(1),glm::vec3(x,0,0))*transform;
            b.addModel(models[i],doc.entities[i].id,doc.entities[i].name,transform,doc.entities[i].tint,doc.entities[i].castsShadow);}
        return b.finish();
    };
    auto compare=[&](const pathtracer::RenderImage& a,const pathtracer::RenderImage& b){
        require(a.sampleCounts==b.sampleCounts && a.primaryHitCount==b.primaryHitCount,"Restart sample/coverage differs from fresh backend");
        // Predeclared float oracle: relative RMS 1e-5 for RT arithmetic; counts exact.
        auto plane=[&](const auto& x,const auto& y){double d=0,scale=0;require(x.size()==y.size(),"AOV extent mismatch");
            for(std::size_t i=0;i<x.size();++i){const auto delta=x[i]-y[i];if constexpr(std::is_arithmetic_v<std::decay_t<decltype(delta)>>){d+=delta*delta;scale+=y[i]*y[i];}else{d+=glm::dot(delta,delta);scale+=glm::dot(y[i],y[i]);}}
            require(std::isfinite(d) && std::sqrt(d/std::max(scale,1.0))<=1e-5,"Restart AOV differs from fresh backend");};
        plane(a.sum,b.sum);plane(a.albedoSum,b.albedoSum);plane(a.normalSum,b.normalSum);plane(a.depthSum,b.depthSum);plane(a.directSum,b.directSum);plane(a.indirectSum,b.indirectSum);plane(a.luminanceSum,b.luminanceSum);plane(a.luminanceSquaredSum,b.luminanceSquaredSum);
    };
    for(int round=0;round<2;++round){
        std::cerr<<"Lifecycle round "<<round<<" create\n";
        iris::vulkan::VulkanPathTracer pt(root/"shaders/vulkan",snapshot(0,0),settings,true);
        std::cerr<<"Lifecycle initial accumulate\n";
        require(std::string(pt.raygenShaderName())==(indexed?"pt.indexed.rgen.spv":"pt.rgen.spv"),"Unexpected PT geometry layout program");
        const auto baseBytes=pt.statistics().allocatedBytes;while(pt.renderPass()){}
        std::cerr<<"Lifecycle initial complete, display checks\n";
        const auto raw=pt.image();const auto samplingGpuMilliseconds=pt.statistics().gpuMilliseconds;
        auto displayCheck=[&](const std::vector<unsigned char>& expected){
            require(expected.size()==pt.previewRgba().size(),"Preview extent mismatch");
            int maximum=0;for(std::size_t i=0;i<expected.size();++i)maximum=std::max(maximum,std::abs(static_cast<int>(expected[i])-pt.previewRgba()[i]));
            require(maximum<=1,"GPU preview differs from CPU display by more than one quantization step");
        };
        displayCheck(pathtracer::makeDisplayRgba8BottomUp(raw,pathtracer::RenderOutput::Beauty));
        const auto dispatches=pt.statistics().previewDispatches;const auto rawBytes=pt.statistics().rawReadbackBytes;
        for(int i=0;i<30;++i)require(!pt.renderPreview(1,true,false),"Completed unchanged image was converted again");
        require(pt.statistics().previewDispatches==dispatches && pt.statistics().rawReadbackBytes==rawBytes,"Completed image repeated GPU work/readback");
        require(pt.renderPreview(2,true,true) && pt.completedSamples()==4,"Exposure change restarted sampling");
        auto exposed=raw;for(auto& p:exposed.sum)p*=2;
        displayCheck(pathtracer::makeDisplayRgba8BottomUp(exposed,pathtracer::RenderOutput::Beauty));
        require(pt.image().sum==raw.sum && pt.statistics().rawReadbackBytes==rawBytes,"Display-only update changed/read Raw");
        require(pt.renderPreview(1,false,true) && pt.completedSamples()==4,"Tone mapping change restarted sampling");
        auto linear=raw;linear.albedoSum=raw.sum;linear.primaryHitCount=raw.sampleCounts;
        displayCheck(pathtracer::makeDisplayRgba8BottomUp(linear,pathtracer::RenderOutput::Albedo));
        require(pt.renderPreview(1,true,true),"Default display restore did not resolve");
        require(pt.statistics().gpuMilliseconds==samplingGpuMilliseconds && pt.image().statistics.renderMilliseconds==raw.statistics.renderMilliseconds,"Display-only work overwrote sampling timings");

        for(int phase=0;phase<8;++phase){
            std::cerr<<"Lifecycle phase "<<phase<<"\n";
            const float yaw=phase==0?2.f:0.f,x=phase==2?.2f:0.f;
            settings.width=phase==4?80:96;settings.height=phase==4?45:54;
            if(phase==6)++settings.seed;
            auto scene=snapshot(yaw,x);require(pt.restart(scene,settings),"Compatible restart rebuilt backend");
            require(pt.image().completedSamples==0,"Restart kept old accumulation");
            std::atomic<bool> cancel{true};require(!pt.renderPass(&cancel) && pt.image().completedSamples==0,"Cancelled restart published samples");
            require(pt.renderPass(),"Restart did not render");
            const auto& stats=pt.statistics();require(stats.blasBuilds==0 && stats.tlasBuilds==0,"Restart rebuilt BLAS/TLAS");
            require(stats.tlasUpdates==((phase==2 || phase==3)?1U:0U),"Rigid restart did not update only TLAS");
            require(pt.image().completedSamples==1,"Restart did not return to SPP 1");
            if(phase!=4)require(stats.allocatedBytes==baseBytes,"Resize round trip leaked resources");
            iris::vulkan::VulkanPathTracer fresh(root/"shaders/vulkan",scene,settings,true);require(fresh.renderPass(),"Fresh reference failed");compare(pt.image(),fresh.image());
            require(!stats.validationWarnings && !stats.validationErrors && stats.synchronizationValidation,"Lifecycle Core/Sync failed");
        }
        std::cerr<<"Lifecycle async poll\n";
        require(pt.restart(snapshot(0,0),settings),"Async restart failed");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        unsigned polls=0;
        while(pt.completedSamples()<settings.samplesPerPixel && std::chrono::steady_clock::now()<deadline){
            pt.renderPreview(1,true,false);++polls;std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(pt.completedSamples()==settings.samplesPerPixel,"Async preview failed to make progress");
        require(pt.statistics().rawReadbackBytes==0,"Async preview read full AOVs");
        require(pt.statistics().readbackBytes==static_cast<std::uint64_t>(settings.width)*settings.height*4+4,"Preview is not bounded RGBA+status");
        const auto asyncImage=pt.image();require(asyncImage.completedSamples==4,"Lazy Raw export lost samples");
        const auto materialized=pt.statistics().rawReadbackBytes;pt.image();require(pt.statistics().rawReadbackBytes==materialized,"Unchanged Raw export was read again");
        std::cerr<<"Lifecycle large pending discard\n";
        settings.width=1024;settings.height=578;
        require(pt.restart(snapshot(0,0),settings),"Restart before stale pending test failed");
        require(!pt.renderPreview(1,true,false),"Large asynchronous dispatch unexpectedly blocked to completion");
        settings.width=96;settings.height=54;
        require(pt.restart(snapshot(2,0),settings) && pt.completedSamples()==0,"Pending old sample published across restart");
        require(pt.renderPreview(1,true,true) && pt.completedSamples()==1,"New generation failed after pending discard");
        std::cout<<"Async preview polls="<<polls<<" completed reuse/display-only/lazy Raw/pending restart PASS\n";
        std::cerr<<"Lifecycle invalid and volume refusal\n";
        auto invalid=settings;invalid.width=0;bool rejected=false;try{pt.restart(snapshot(0,0),invalid);}catch(const std::exception&){rejected=true;}
        require(rejected && pt.image().completedSamples==1,"Invalid restart destroyed current image");
        auto& material=models[0]->materials.at(0);const float ior=material.indexOfRefraction;material.indexOfRefraction+=.1f;
        require(!pt.restart(snapshot(0,0),settings) && pt.image().completedSamples==1,"Volume edit reused stale resources");material.indexOfRefraction=ior;
        for(auto output:{pathtracer::RenderOutput::Beauty,pathtracer::RenderOutput::Albedo,pathtracer::RenderOutput::Normal,pathtracer::RenderOutput::Depth,pathtracer::RenderOutput::Direct,pathtracer::RenderOutput::Indirect,pathtracer::RenderOutput::SampleCount,pathtracer::RenderOutput::Variance})
            pathtracer::writeRenderOutput(pt.image(),output,out/("lifecycle-"+std::to_string(round)+"-"+std::to_string(static_cast<int>(output))),{pathtracer::RenderFileFormat::OpenExr});
    }
    std::cerr<<"Lifecycle GPU finite flag\n";
    {auto c=snapshot(0,0).camera();pathtracer::SceneSnapshotLighting l;l.environment.backgroundColor=glm::vec3(1e30f);
        pathtracer::SceneSnapshotBuilder builder(c,l);auto tiny=settings;tiny.width=2;tiny.height=1;
        iris::vulkan::VulkanPathTracer invalid(root/"shaders/vulkan",builder.finish(),tiny,true);bool rejected=false;
        try{invalid.renderPreview(1,true,true);}catch(const std::exception& e){rejected=std::string(e.what()).find("Nonfinite")!=std::string::npos;}
        require(rejected && invalid.completedSamples()==0,"GPU nonfinite moment reached preview publication");
        bool rawRefused=false;try{invalid.image();}catch(const std::exception&){rawRefused=true;}require(rawRefused,"Failed GPU accumulation exposed Raw output");
    }
    {auto camera=snapshot(0,0).camera();pathtracer::SceneSnapshotLighting light;light.environment.backgroundColor=glm::vec3(2);
        pathtracer::SceneSnapshotBuilder builder(camera,light);auto empty=builder.finish();auto tiny=settings;tiny.width=tiny.height=1;tiny.samplesPerPixel=2;
        iris::vulkan::VulkanPathTracer display(root/"shaders/vulkan",empty,tiny,true);
        require(display.restart(empty,tiny),"1x1 restart failed");
        bool rejected=false;try{display.renderPreview(std::numeric_limits<float>::max(),true,true);}catch(const std::exception& e){rejected=std::string(e.what()).find("Invalid GPU PT display")!=std::string::npos;}
        require(rejected && display.completedSamples()==1 && display.image().sampleCounts[0]==1,"Display error lost an in-progress Raw sample");
        require(display.renderPreview(1,true,true) && display.completedSamples()==2,"In-progress display did not recover");
        const auto raw=display.image().sum;require(raw[0]==glm::vec3(4) && display.image().sampleCounts[0]==2,"Display recovery duplicated/skipped a sample");
        rejected=false;try{display.renderPreview(std::numeric_limits<float>::max(),true,true);}catch(const std::exception& e){rejected=std::string(e.what()).find("Invalid GPU PT display")!=std::string::npos;}
        require(rejected && display.completedSamples()==2,"Display-only failure reset Raw samples");
        require(display.renderPreview(1,true,true) && display.completedSamples()==2 && display.image().sum==raw,"Corrected display failed to recover without resampling");
        require(display.statistics().rawReadbackBytes==256,"Display-only recovery reread full AOVs");
        std::cout<<"GPU display error recovery, sample-index preservation and 1x1 boundary PASS\n";
    }
    queueAcceptance(out,scenePath);
    if(indexed){
        const auto missing=out/"missing-indexed";std::filesystem::create_directories(missing);
        for(const auto file:{"pt.rgen.spv","pt.rmiss.spv","pt.rchit.spv"})std::filesystem::copy_file(root/"shaders/vulkan"/file,missing/file);
        bool refused=false;try{iris::vulkan::VulkanPathTracer invalid(missing,snapshot(0,0),settings,true);}catch(const std::exception& e){refused=std::string(e.what()).find("pt.indexed.rgen.spv")!=std::string::npos;}
        require(refused,"Missing indexed program silently changed geometry layout");
        std::cout<<"Missing selected indexed shader refused PASS\n";
    }
    std::cout<<"GPU PT lifecycle PASS: "<<(indexed?"Intel Sponza indexed":"Stanford glass legacy")<<", two lifetimes, camera/rigid/resize/seed/restart, fresh-backend eight-AOV oracle, cancellation, invalid/volume edit refusal, bounded allocations; Core/Sync warnings=0 errors=0\n";
    return 0;
}

int extentAcceptance(const std::filesystem::path& root) {
    pathtracer::SnapshotCamera camera;
    camera.view=glm::mat4(1);camera.verticalFieldOfViewRadians=glm::radians(60.f);
    pathtracer::SceneSnapshotLighting lighting;lighting.environment.backgroundColor=glm::vec3(2);
    pathtracer::SceneSnapshotBuilder builder(camera,lighting);const auto scene=builder.finish();
    pathtracer::RenderSettings settings;settings.width=1280;settings.height=720;settings.samplesPerPixel=1;settings.maxDepth=8;
    for(int lifetime=0;lifetime<2;++lifetime) {
        iris::vulkan::VulkanPathTracer pt(root/"shaders/vulkan",scene,settings,true);
        const auto checkImage=[&] {
            require(pt.renderPreview(1,true,true) && pt.completedSamples()==1,"Extent failed to complete one sample");
            const auto count=static_cast<std::size_t>(settings.width)*settings.height;
            require(pt.previewRgba().size()==count*4,"Extent preview byte count mismatch");
            const auto previewBytes=std::max<std::size_t>(count+1,4)*4;
            require(pt.statistics().previewReadbackBytes==previewBytes,"Extent preview/status capacity mismatch");
            const auto& image=pt.image();
            require(image.width==settings.width && image.height==settings.height && image.sum.size()==count,"Extent was scaled or truncated");
            require(pt.statistics().rawReadbackBytes==count*128,"Extent Raw buffer capacity mismatch");
            require(image.sampleCounts.size()==count && image.normalSum.size()==count && image.depthSum.size()==count && image.luminanceSquaredSum.size()==count,"Extent AOV capacity mismatch");
            for(std::size_t i=0;i<count;++i)require(image.sampleCounts[i]==1 && image.sum[i]==glm::vec3(2),"Extent lost a pixel or changed the analytic environment");
            require(pt.statistics().validationEnabled && pt.statistics().synchronizationValidation && !pt.statistics().validationWarnings && !pt.statistics().validationErrors,"Extent Strict Core/Sync failed");
            std::cout<<"GPU PT extent "<<settings.width<<"x"<<settings.height<<" pixels="<<count<<" raw="<<count*128<<" preview="<<previewBytes<<" allocated="<<pt.statistics().allocatedBytes<<" PASS\n";
        };
        checkImage();
        for(const auto size:{glm::uvec2(720,1280),glm::uvec2(1024,1024),glm::uvec2(1,1),glm::uvec2(1280,720)}) {
            settings.width=size.x;settings.height=size.y;
            require(pt.restart(scene,settings) && pt.completedSamples()==0,"Extent resize did not reset history");
            checkImage();
        }
        const auto bytes=pt.statistics().allocatedBytes;
        for(const auto size:{glm::uvec2(0,720),glm::uvec2(1281,720),glm::uvec2(1280,1281),glm::uvec2(std::numeric_limits<unsigned>::max(),std::numeric_limits<unsigned>::max())}) {
            auto invalid=settings;invalid.width=size.x;invalid.height=size.y;bool rejected=false;
            try{pt.restart(scene,invalid);}catch(const std::exception&){rejected=true;}
            require(rejected && pt.completedSamples()==1 && pt.statistics().allocatedBytes==bytes && pt.image().sum[0]==glm::vec3(2),"Invalid extent changed the current image/resources");
            rejected=false;try{iris::vulkan::VulkanPathTracer bad(root/"shaders/vulkan",scene,invalid,true);}catch(const std::exception&){rejected=true;}
            require(rejected,"Invalid constructor extent was accepted");
        }
    }
    std::cout<<"GPU PT extent PASS: native720p/portrait/legacy1024square/1x1, two lifetimes, exact analytic pixels, Raw/AOV capacity, invalid/overflow rejection retains image; Core/Sync warnings=0 errors=0\n";
    return 0;
}
}
int guideAcceptance(const std::filesystem::path& root,const std::filesystem::path& out){
    using iris::vulkan::VulkanPathTracer;
    pathtracer::RenderSettings settings;settings.width=127;settings.height=73;settings.samplesPerPixel=2;settings.maxDepth=3;settings.seed=20261028;
    auto model=std::make_shared<ModelData>(makeGroundPlaneData());model->materials[0].transmissionFactor=.5f;
    constexpr std::uint64_t entity=0x1234567887654321ULL;
    glm::mat4 pose(1),oldPose(1);float cameraX=0,fov=70;glm::mat4 oldView(1);
    auto snapshot=[&](){pathtracer::SnapshotCamera camera;camera.position={cameraX,5,0};camera.view=glm::lookAt(camera.position,glm::vec3(cameraX,0,0),glm::vec3(0,0,-1));camera.verticalFieldOfViewRadians=glm::radians(fov);
        pathtracer::SceneSnapshotLighting light;light.environment.backgroundColor={.2f,.3f,.4f};
        pathtracer::SceneSnapshotBuilder builder(camera,light);builder.addModel(model,entity,"guide plane",pose,glm::vec3(1),true);return builder.finish();};
    auto scene=snapshot();oldView=scene.camera().view;
    VulkanPathTracer pt(root/"shaders/vulkan",scene,settings,true);
    auto sample=[&](){while(pt.renderPass(nullptr,false)){};};require(pt.renderPass(nullptr,false),"Initial guide sample failed");
    const auto initial=pt.image();const auto rawBytes=pt.statistics().rawReadbackBytes,baseBytes=pt.statistics().allocatedBytes;
    auto first=pt.temporalGuides();require(!first.historyValid && first.pixels.size()==127*73,"First guide frame reused history");
    require(pt.statistics().allocatedBytes==baseBytes+first.allocatedBytes,"Optional guide memory missing from total statistics");
    require(pt.completedSamples()==1 && pt.statistics().rawReadbackBytes==rawBytes,"Guide pass modified integrator/readback");
    auto same=pt.temporalGuides();require(std::memcmp(same.pixels.data(),first.pixels.data(),same.pixels.size()*sizeof(same.pixels[0]))==0,"Guide cache changed paused frame");
    const auto& unchanged=pt.image();require(initial.sum==unchanged.sum && initial.albedoSum==unchanged.albedoSum && initial.normalSum==unchanged.normalSum && initial.depthSum==unchanged.depthSum && initial.directSum==unchanged.directSum && initial.indirectSum==unchanged.indirectSum && initial.sampleCounts==unchanged.sampleCounts && initial.primaryHitCount==unchanged.primaryHitCount && initial.luminanceSum==unchanged.luminanceSum && initial.luminanceSquaredSum==unchanged.luminanceSquaredSum,"Guide pass changed original AOVs");
    sample();{VulkanPathTracer reference(root/"shaders/vulkan",scene,settings,true);while(reference.renderPass()){};
        const auto& a=pt.image();const auto& z=reference.image();require(a.sum==z.sum && a.albedoSum==z.albedoSum && a.normalSum==z.normalSum && a.depthSum==z.depthSum && a.directSum==z.directSum && a.indirectSum==z.indirectSum && a.sampleCounts==z.sampleCounts && a.primaryHitCount==z.primaryHitCount && a.luminanceSum==z.luminanceSum && a.luminanceSquaredSum==z.luminanceSquaredSum,"Guides between SPPs changed integrator");}
    const auto checkGuides=[&](const iris::vulkan::TemporalGuideFrame& guides,bool history){
        require(guides.historyValid==history,"Guide frame history validity incorrect");
        const auto current=snapshot().camera();const auto inv=glm::inverse(current.view);const auto inversePose=glm::inverse(pose);
        std::size_t hits=0,valid=0;double maximum=0;
        for(unsigned y=0;y<settings.height;++y)for(unsigned x=0;x<settings.width;++x){
            const auto& p=guides.pixels[y*settings.width+x];const auto flags=static_cast<unsigned>(p.normal.w);
            const float tanFov=std::tan(current.verticalFieldOfViewRadians*.5f),aspect=static_cast<float>(settings.width)/settings.height;
            const auto direction=glm::normalize(glm::mat3(inv)*glm::vec3((2*(x+.5f)/settings.width-1)*aspect*tanFov,(1-2*(y+.5f)/settings.height)*tanFov,-1));
            const auto origin=glm::vec3(inversePose*glm::vec4(current.position,1));const auto ray=glm::vec3(inversePose*glm::vec4(direction,0));
            const float distance=-origin.y/ray.y;const auto local=origin+distance*ray;
            const bool hit=distance>0 && std::abs(local.x)<4 && std::abs(local.z)<4;
            require(((flags&1U)!=0)==hit,"Guide coverage differs from analytic plane");
            if(!hit){require(p.identity==glm::uvec4(0) && p.motionDepth==glm::vec4(0),"Miss inherited stale guides");continue;}
            ++hits;require(p.identity==glm::uvec4(static_cast<unsigned>(entity),static_cast<unsigned>(entity>>32),0,1) && p.previousNormal.w==1,"Guide identity lost entity/instance/mesh/material");require((flags&4U)!=0,"Transmission front-surface flag missing");
            auto n=glm::normalize(glm::transpose(glm::inverse(glm::mat3(pose)))*glm::vec3(0,1,0));if(glm::dot(n,direction)>0)n=-n;
            require(glm::length(glm::vec3(p.normal)-n)<2e-5f,"Guide geometric normal incorrect");
            const auto world=pose*glm::vec4(local,1);const auto previous=oldView*oldPose*glm::vec4(local,1);const float depth=-(current.view*world).z,previousDepth=-previous.z;
            require(std::abs(p.motionDepth.z-depth)<2e-4f,"Guide linear depth incorrect");
            if(!history){require((flags&2U)==0 && p.motionDepth.x==0 && p.motionDepth.y==0 && p.motionDepth.w==0,"Invalid history emitted motion");continue;}
            const glm::vec2 oldPixel((.5f+.5f*previous.x/(previousDepth*aspect*tanFov))*settings.width,(.5f-.5f*previous.y/(previousDepth*tanFov))*settings.height);
            const bool in=previousDepth>1e-6f && oldPixel.x>=0 && oldPixel.y>=0 && oldPixel.x<settings.width && oldPixel.y<settings.height;
            require(((flags&2U)!=0)==in,"Offscreen reprojection validity incorrect");
            if(in){++valid;maximum=std::max(maximum,static_cast<double>(glm::length(glm::vec2(p.motionDepth)-(oldPixel-glm::vec2(x+.5f,y+.5f)))));require(std::abs(p.motionDepth.w-previousDepth)<2e-4f,"Predicted previous depth incorrect");
                auto oldNormal=glm::normalize(glm::transpose(glm::inverse(glm::mat3(oldPose)))*glm::vec3(0,1,0));if(glm::dot(oldNormal,glm::vec3(0,-1,0))>0)oldNormal=-oldNormal;
                require(glm::length(glm::vec3(p.previousNormal)-oldNormal)<2e-5f,"Previous normal transform incorrect");}
        }
        require(hits>100 && (!history || valid>100) && maximum<.002,"Analytic guide reprojection exceeded 0.002 pixel budget");
        std::cout<<"Guides frame="<<guides.frame<<" history="<<history<<" hits="<<hits<<" valid="<<valid<<" maxPixelError="<<maximum<<" gpuMs="<<guides.gpuMilliseconds<<" allocated="<<guides.allocatedBytes<<" PASS\n";
    };
    checkGuides(first,false);
    for(int step=0;step<4;++step){oldPose=pose;oldView=snapshot().camera().view;
        if(step==0)pose=glm::translate(glm::mat4(1),glm::vec3(.3f,0,0));
        if(step==1)cameraX=.2f;
        if(step==2)pose=glm::translate(glm::mat4(1),glm::vec3(.1f,0,.1f))*glm::rotate(glm::mat4(1),.12f,glm::vec3(0,0,1))*glm::scale(glm::mat4(1),glm::vec3(1.2f,.8f,1));
        if(step==3)pose=glm::translate(glm::mat4(1),glm::vec3(-2.f,0,0))*glm::scale(glm::mat4(1),glm::vec3(-1.f,1.f,1.f));
        require(pt.restart(snapshot(),settings),"Guide rigid/camera restart failed");pt.setTemporalFrame(step+1,30);sample();auto guides=pt.temporalGuides();checkGuides(guides,true);
        require(guides.allocatedBytes==first.allocatedBytes,"Guide same-extent allocations grew across motion");
        if(step==1){pathtracer::RenderImage display;display.width=settings.width;display.height=settings.height;display.completedSamples=1;display.sampleCounts.assign(guides.pixels.size(),1);for(const auto& p:guides.pixels)display.sum.emplace_back(.5f+.07f*p.motionDepth.x,.5f+.07f*p.motionDepth.y,(static_cast<unsigned>(p.normal.w)&2U)?1.f:0.f);pathtracer::writeRenderOutput(display,pathtracer::RenderOutput::Beauty,out/"motion",{pathtracer::RenderFileFormat::Png});}
    }
    oldPose=pose;oldView=snapshot().camera().view;require(pt.restart(snapshot(),settings),"Paused guide restart failed");sample();checkGuides(pt.temporalGuides(),true);
    for(const auto& p:pt.temporalGuides().pixels)require(glm::length(glm::vec2(p.motionDepth))<.0002f,"Static guides contain motion/jitter");
    const auto invalid=[&](int frame,int fps,bool cut){pt.setTemporalFrame(frame,fps,cut);checkGuides(pt.temporalGuides(),false);};
    invalid(8,30,false);invalid(7,30,false);invalid(8,24,false);invalid(8,24,true);
    pt.invalidateTemporalHistory();checkGuides(pt.temporalGuides(),false);
    bool rejected=false;try{pt.setTemporalFrame(8,0);}catch(const std::exception&){rejected=true;}require(rejected,"Invalid temporal FPS accepted");
    std::atomic<bool> cancel{true};require(!pt.renderPass(&cancel),"Guide cancellation ignored");rejected=false;try{pt.temporalGuides();}catch(const std::exception&){rejected=true;}require(rejected,"Cancelled guide packet published");
    require(pt.restart(snapshot(),settings),"Guide cancellation recovery failed");sample();checkGuides(pt.temporalGuides(),false);
    ++settings.seed;require(pt.restart(snapshot(),settings),"Seed restart failed");sample();checkGuides(pt.temporalGuides(),false);
    fov=80;require(pt.restart(snapshot(),settings),"Lens change restart failed");sample();checkGuides(pt.temporalGuides(),false);
    {auto camera=snapshot().camera();camera.view=glm::rotate(glm::mat4(1),glm::radians(60.f),glm::vec3(0,1,0))*camera.view;
        pathtracer::SceneSnapshotBuilder cut(camera,snapshot().lighting());cut.addModel(model,entity,"guide plane",pose,glm::vec3(1),true);
        require(pt.restart(cut.finish(),settings),"Automatic camera cut restart failed");sample();require(!pt.temporalGuides().historyValid,"Large orientation cut reused history");
        require(pt.restart(snapshot(),settings),"Camera cut return failed");sample();checkGuides(pt.temporalGuides(),false);}
    const auto beforeInvalid=pt.image();auto materialEdit=snapshot();model->materials[0].roughnessFactor=.21f;
    require(!pt.restart(materialEdit,settings),"Material edit retained guide resources/history");model->materials[0].roughnessFactor=.82f;
    require(pt.image().sum==beforeInvalid.sum,"Rejected material edit changed Raw image");
    settings.width=1280;settings.height=720;require(pt.restart(snapshot(),settings),"720p guide restart failed");sample();checkGuides(pt.temporalGuides(),false);
    require(pt.statistics().validationErrors==0 && pt.statistics().validationWarnings==0,"Guide lifecycle validation diagnostics");
    const auto isolated=out/"missing-guide-shader";std::filesystem::create_directories(isolated);
    for(const auto file:{"pt.rgen.spv","pt.rmiss.spv","pt.rchit.spv"})std::filesystem::copy_file(root/"shaders/vulkan"/file,isolated/file);
    settings.width=31;settings.height=19;
    {VulkanPathTracer missing(isolated,snapshot(),settings,true);while(missing.renderPass()){};const auto before=missing.image().sum;
        rejected=false;try{missing.temporalGuides();}catch(const std::exception&){rejected=true;}require(rejected && missing.image().sum==before,"Missing guide shader damaged Raw image");
        std::filesystem::copy_file(root/"shaders/vulkan/pt-guides.comp.spv",isolated/"pt-guides.comp.spv");require(!missing.temporalGuides().historyValid,"Recovered guide shader reused invalid history");}
    {pathtracer::SceneSnapshotBuilder empty(snapshot().camera(),snapshot().lighting());VulkanPathTracer emptyPt(root/"shaders/vulkan",empty.finish(),settings,true);while(emptyPt.renderPass()){};
        const auto& guides=emptyPt.temporalGuides();for(const auto& p:guides.pixels)require(p.motionDepth==glm::vec4(0) && p.normal==glm::vec4(0) && p.previousNormal==glm::vec4(0) && p.identity==glm::uvec4(0),"Empty Scene retained surface guides");
        require(emptyPt.statistics().validationWarnings==0 && emptyPt.statistics().validationErrors==0,"Empty guide validation diagnostics");}
    {settings.width=128;settings.height=72;auto checker=std::make_shared<ModelData>(makeGlassCheckerboardData());
        pathtracer::SnapshotCamera camera;camera.position={0,0,5};camera.view=glm::lookAt(camera.position,glm::vec3(0,0,0),glm::vec3(0,1,0));camera.verticalFieldOfViewRadians=glm::radians(40.f);
        pathtracer::SceneSnapshotBuilder builder(camera,snapshot().lighting());builder.addModel(checker,entity,"material edges",glm::mat4(1),glm::vec3(1),true);
        VulkanPathTracer checkerPt(root/"shaders/vulkan",builder.finish(),settings,true);while(checkerPt.renderPass()){};const auto& guides=checkerPt.temporalGuides();
        unsigned materials[2]{};pathtracer::RenderImage display;display.width=settings.width;display.height=settings.height;display.completedSamples=1;display.sampleCounts.assign(guides.pixels.size(),1);
        for(unsigned y=0;y<settings.height;++y)for(unsigned x=0;x<settings.width;++x){const auto& p=guides.pixels[y*settings.width+x];
            const float tangent=std::tan(camera.verticalFieldOfViewRadians*.5f),z=6.05f;
            const float wx=(2*(x+.5f)/settings.width-1)*static_cast<float>(settings.width)/settings.height*tangent*z;
            const float wy=(1-2*(y+.5f)/settings.height)*tangent*z;
            const int col=static_cast<int>(std::floor(wx/.34f+5)),row=static_cast<int>(std::floor(wy/.34f+3.5f));
            const bool inside=col>=0 && col<10 && row>=0 && row<7;
            require(((static_cast<unsigned>(p.normal.w)&1U)!=0)==inside,"Material edge analytic coverage mismatch");
            if(inside){const unsigned material=(row+col)%2==0?2U:1U;require(p.identity.w==material,"Material edge blended/misidentified");++materials[material-1];}
            display.sum.push_back(!inside?glm::vec3(0):p.identity.w==1?glm::vec3(1,.1f,.05f):glm::vec3(.05f,.4f,1));
        }
        require(materials[0]>100 && materials[1]>100,"Material edge fixture lacks both materials");pathtracer::writeRenderOutput(display,pathtracer::RenderOutput::Beauty,out/"material-identity",{pathtracer::RenderFileFormat::Png});
        require(checkerPt.statistics().validationWarnings==0 && checkerPt.statistics().validationErrors==0,"Material edge validation diagnostics");}
    std::cout<<"GPU PT guides PASS: analytic coverage/depth/normal/identity/motion, rigid/camera/nonuniform/mirrored poses, offscreen/cuts/time/FPS/seed/resize/cancel/recovery, cached paused packet; strict validation\n";return 0;
}
#include "VulkanSpatialAcceptance.inc"
#include "VulkanTemporalAcceptance.inc"
int main(int argc,char** argv){try{
    require(argc==3 || (argc==4 && (std::string(argv[3])=="--lifecycle" || std::string(argv[3])=="--indexed-lifecycle" || std::string(argv[3])=="--extent" || std::string(argv[3])=="--guides" || std::string(argv[3])=="--spatial" || std::string(argv[3])=="--temporal" || std::string(argv[3])=="--temporal-scenes")),"Usage: IrisVulkanPathTracingAcceptance <runtime root> <output directory> [--lifecycle|--indexed-lifecycle|--extent|--guides|--spatial|--temporal|--temporal-scenes]");
    const auto root=std::filesystem::absolute(argv[1]),out=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(out);
    require(glfwInit()==GLFW_TRUE,"GLFW initialization failed");struct Lifetime{~Lifetime(){glfwTerminate();}} lifetime;
    if(argc==4 && std::string(argv[3])=="--extent")return extentAcceptance(root);
    if(argc==4 && std::string(argv[3])=="--guides")return guideAcceptance(root,out);
    if(argc==4 && std::string(argv[3])=="--spatial")return spatialAcceptance(root,out);
    if(argc==4 && std::string(argv[3])=="--temporal")return temporalAcceptance(root,out);
    if(argc==4 && std::string(argv[3])=="--temporal-scenes")return temporalSceneAcceptance(root,out);
    if(argc==4)return lifecycle(root,out,std::string(argv[3])=="--indexed-lifecycle");
    pathtracer::SnapshotCamera camera;camera.position={6,6,9};camera.view=glm::lookAt(camera.position,glm::vec3(0,.5f,0),glm::vec3(0,1,0));
    pathtracer::SceneSnapshotLighting lighting;lighting.environment.backgroundColor={.2f,.3f,.4f};
    auto floor=std::make_shared<ModelData>(makeGroundPlaneData());ObjLoader loader;auto cube=std::make_shared<ModelData>(loader.load(root/"assets/models/cube.obj").model);
    if(cube->materials.empty())cube->materials.emplace_back();for(auto& m:cube->meshes)for(auto& p:m.submeshes)if(p.materialIndex<0)p.materialIndex=0;
    lighting.directional.radiance={.5f,.5f,.5f};
    pathtracer::SnapshotLocalLight point;point.position={2,4,1};point.radiance={2,1,1};point.radius=12;lighting.localLights.push_back(point);
    cube->materials[0].metallicFactor=.65f;cube->materials[0].roughnessFactor=.3f;cube->materials[0].emissiveFactor={.03f,.01f,.005f};
    auto spot=point;spot.type=pathtracer::SnapshotLocalLightType::Spot;spot.position={-2,4,0};spot.radiance={1,2,1};lighting.localLights.push_back(spot);
    auto scene=[&](bool empty=false,bool mirror=false){pathtracer::SceneSnapshotBuilder b(camera,lighting);if(!empty){b.addModel(floor,1,"floor",glm::mat4(1));b.addModel(cube,2,"cube",glm::translate(glm::mat4(1),glm::vec3(0,1,0))*glm::scale(glm::mat4(1),glm::vec3(mirror?-1.f:1.f,1,1)));}return b.finish();};
    pathtracer::RenderSettings settings;settings.width=64;settings.height=36;settings.samplesPerPixel=64;settings.maxDepth=4;settings.workerCount=2;
    if(std::getenv("IRIS_VULKAN_DISABLE_PATH_TRACING")){bool rejected=false;try{iris::vulkan::VulkanPathTracer pt(root/"shaders/vulkan",scene(),settings,true);}catch(const std::exception& e){std::cout<<e.what()<<'\n';rejected=true;}require(rejected,"Missing capability accepted");return 0;}
    std::vector<glm::vec3> previous;double firstError=0,middleError=0,lastError=0;std::uint64_t memory=0;
    auto referenceSettings=settings;referenceSettings.samplesPerPixel=1024;
    pathtracer::ProgressiveRenderer cpu(scene(),referenceSettings);while(cpu.renderPass()){}
    for(int round=0;round<2;++round){
        iris::vulkan::VulkanPathTracer pt(root/"shaders/vulkan",scene(),settings,true);
        std::atomic<bool> cancel{true};require(!pt.renderPass(&cancel) && pt.image().completedSamples==0,"Cancelled pass advanced");cancel=false;
        require(pt.renderPass(),"First sample missing");firstError=error(pt.image(),cpu.image());
        pathtracer::writeRenderOutput(pt.image(),pathtracer::RenderOutput::Beauty,out/("round"+std::to_string(round)+"-spp1"),{pathtracer::RenderFileFormat::Png});
        for(unsigned s=1;s<64;++s){require(pt.renderPass(),"Incomplete progressive pass");if(s==7){middleError=error(pt.image(),cpu.image());pathtracer::writeRenderOutput(pt.image(),pathtracer::RenderOutput::Beauty,out/("round"+std::to_string(round)+"-spp8"),{pathtracer::RenderFileFormat::Png});}}
        require(!pt.renderPass(),"Target SPP exceeded");
        const auto& image=pt.image();const auto pixels=image.linearPixels();
        for(auto output:{pathtracer::RenderOutput::Albedo,pathtracer::RenderOutput::Normal,pathtracer::RenderOutput::Depth,pathtracer::RenderOutput::Direct,pathtracer::RenderOutput::Indirect})
            pathtracer::writeRenderOutput(image,output,out/("lifetime-"+std::to_string(round)+"-"+std::to_string(static_cast<int>(output))),{pathtracer::RenderFileFormat::OpenExr});
        pathtracer::writeRenderOutput(image,pathtracer::RenderOutput::Beauty,out/("deterministic-"+std::to_string(round)),{pathtracer::RenderFileFormat::OpenExr});
        if(round){double maximum=0;std::size_t changed=0;for(std::size_t i=0;i<pixels.size();++i){const auto difference=glm::length(pixels[i]-previous[i]);maximum=std::max(maximum,static_cast<double>(difference));changed+=difference>0;}std::cout<<"Lifetime difference: changed="<<changed<<" maximum="<<maximum<<"\n";
            require(std::memcmp(previous.data(),pixels.data(),pixels.size()*sizeof(glm::vec3))==0,"Fixed seed differs across device lifetimes");}previous=pixels;
        const auto direct=image.directPixels(),indirect=image.indirectPixels();unsigned hits=0;
        for(std::size_t i=0;i<pixels.size();++i){require(image.sampleCounts[i]==64,"Incorrect sample count");hits+=image.primaryHitCount[i]>0;
            require(glm::length(pixels[i]-direct[i]-indirect[i])<1e-4f,"Lighting AOV sum mismatch");
            require(std::isfinite(image.depthSum[i]) && std::isfinite(image.luminanceSquaredSum[i]),"Nonfinite AOV");}
        require(hits>100,"No primary geometry coverage");lastError=error(image,cpu.image());require(lastError<firstError && lastError<.06,"GPU/CPU convergence failed");
        const auto& stats=pt.statistics();require(stats.validationEnabled && stats.synchronizationValidation && !stats.validationErrors && !stats.validationWarnings,"Strict Core/Sync failed");memory=stats.allocatedBytes;
        for(auto output:{pathtracer::RenderOutput::Beauty,pathtracer::RenderOutput::Albedo,pathtracer::RenderOutput::Normal,pathtracer::RenderOutput::Depth,pathtracer::RenderOutput::Direct,pathtracer::RenderOutput::Indirect,pathtracer::RenderOutput::SampleCount,pathtracer::RenderOutput::Variance})
            pathtracer::writeRenderOutput(image,output,out/("round"+std::to_string(round)+"-"+std::to_string(static_cast<int>(output))),{pathtracer::RenderFileFormat::Png,pathtracer::RenderFileFormat::OpenExr});
    }
    {auto s=settings;s.samplesPerPixel=1;iris::vulkan::VulkanPathTracer pt(root/"shaders/vulkan",scene(true),s,true);require(pt.renderPass(),"Empty scene failed");for(auto p:pt.image().linearPixels())require(glm::length(p-lighting.environment.backgroundColor)<1e-6f,"Analytic environment mismatch");}
    {auto s=settings;s.samplesPerPixel=1;iris::vulkan::VulkanPathTracer pt(root/"shaders/vulkan",scene(false,true),s,true);require(pt.renderPass(),"Mirrored instance failed");pathtracer::ProgressiveRenderer mirrored(scene(false,true),s);mirrored.renderPass();const auto a=pt.image().normalPixels(),b=mirrored.image().normalPixels();for(std::size_t i=0;i<a.size();++i)require(glm::length(a[i]-b[i])<1e-3f,"Mirrored normal differs from CPU");}
    for(int unsupported=0;unsupported<5;++unsupported){
        auto& material=cube->materials[0];material.transmissionFactor=0;
        material.alphaMode=unsupported==0?MaterialAlphaMode::Mask:unsupported==1?MaterialAlphaMode::Blend:MaterialAlphaMode::Opaque;
        material.baseColorTextureIndex=unsupported==2?0:-1;material.normalTextureIndex=unsupported==3?0:-1;material.metallicRoughnessTextureIndex=unsupported==4?0:-1;
        bool rejected=false;try{iris::vulkan::VulkanPathTracer pt(root/"shaders/vulkan",scene(),settings,true);}catch(const std::exception&){rejected=true;}require(rejected,"Unsupported material accepted");
    }
    {RenderJob job;std::string message;require(loadRenderJob(root/"assets/renderjobs/21_gpu_path_traced.renderjob",job,message),message.c_str());
        job.renderSettings.width=64;job.renderSettings.height=36;job.renderSettings.samplesPerPixel=65536;job.outputStemPattern=(out/"cancel/frame").generic_string();
        CancellationToken cancel;std::thread worker([&]{std::this_thread::sleep_for(std::chrono::milliseconds(10000));cancel.requestCancellation();});
        BatchFrameResult result;const bool success=runRenderJobFrame(job,0,result,message,&cancel);worker.join();
        std::cout<<"Batch cancellation: status="<<static_cast<int>(result.status)<<" cameraSamples="<<result.statistics.cameraSamples<<" message="<<message<<"\n";
        require(!success && result.status==BatchFrameStatus::Cancelled && result.statistics.cameraSamples>0,"Active Batch cancellation failed");
        require(!std::filesystem::exists(out/"cancel"),"Cancelled Batch published output");}
    {RenderJob job;SceneDocument document;std::string message;
        require(loadRenderJob(root/"assets/renderjobs/21_gpu_path_traced.renderjob",job,message),message.c_str());
        require(loadSceneDocument(job.scenePath,document,message),message.c_str());document.entities.clear();document.renderer.water.enabled=true;
        job.scenePath=out/"unsupported-water.myscene";require(saveSceneDocument(job.scenePath,document,message),message.c_str());
        job.outputStemPattern=out/"unsupported-water/frame";job.renderSettings.samplesPerPixel=1;
        BatchFrameResult result;require(!runRenderJobFrame(job,0,result,message) && message.find("water")!=std::string::npos,"GPU Batch silently ignored unsupported water presentation");
        require(!std::filesystem::exists(out/"unsupported-water"),"Unsupported Scene published outputs");}
    queueAcceptance(out,root/"assets/scenes/fixtures/40_gpu_path_traced.myscene");
    std::cout<<"GPU PT M3-C PASS: two lifetimes, deterministic 64 SPP, analytic environment, mirrored normals, cancellation, eight AOVs, unsupported rejection; RMSE 1="<<firstError<<" 8="<<middleError<<" 64="<<lastError<<" bytes="<<memory<<"; Core/Sync warnings=0 errors=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

