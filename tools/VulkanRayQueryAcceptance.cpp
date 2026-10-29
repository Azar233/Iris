#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include "vulkan/VulkanRaster.h"
#include "pathtracer/SceneSnapshot.h"
#include "asset/BuiltinModels.h"
#include "io/ObjLoader.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "third_party/renderdoc_app.h"
#endif
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::size_t difference(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b){require(a.size()==b.size(),"Extent mismatch");std::size_t n=0;for(std::size_t i=0;i<a.size();++i)n+=a[i]!=b[i];return n;}
void png(const std::filesystem::path& path,const std::vector<unsigned char>& pixels,int width=320,int height=180){require(stbi_write_png(path.string().c_str(),width,height,4,pixels.data(),width*4)!=0,"PNG write failed");}
// Independent shared CPU triangle intersections check the floor visibility mask.
std::pair<unsigned,unsigned> oracle(const pathtracer::SceneSnapshot& scene,const std::vector<unsigned char>& image) {
    const auto triangles=pathtracer::buildWorldTriangles(scene);const auto inverse=glm::inverse(scene.camera().projection*scene.camera().view);
    unsigned checked=0,shadowed=0,mismatch=0;
    const auto visibility=[&](glm::vec3 p){pathtracer::Ray ray{p+glm::vec3(0,0.002f,0),glm::normalize(-scene.lighting().directional.direction),0.001f,100000};
        for(const auto& triangle:triangles){pathtracer::SurfaceInteraction hit;if(pathtracer::intersectTriangle(ray,triangle,hit))return false;}return true;};
    for(int y=0;y<180;++y)for(int x=0;x<320;++x){
        auto p=inverse*glm::vec4((x+0.5f)/160-1,1-(y+0.5f)/90,-1,1);p/=p.w;
        pathtracer::Ray ray{scene.camera().position,glm::normalize(glm::vec3(p)-scene.camera().position)};
        pathtracer::SurfaceInteraction closest;bool floor=false;
        for(const auto& triangle:triangles){pathtracer::SurfaceInteraction hit;if(pathtracer::intersectTriangle(ray,triangle,hit) && hit.t<closest.t){closest=hit;floor=triangle.instanceIndex==0;}}
        if(!floor || std::abs(closest.position.x)>3.9f || std::abs(closest.position.z)>3.9f)continue;
        const bool lit=visibility(closest.position);
        bool boundary=false;for(const auto offset:{glm::vec3(.02f,0,0),glm::vec3(-.02f,0,0),glm::vec3(0,0,.02f),glm::vec3(0,0,-.02f)})boundary|=visibility(closest.position+offset)!=lit;
        if(boundary)continue;
        const auto value=image[(y*320+x)*4];mismatch+=value!=(lit?255:0);++checked;shadowed+=!lit;
    }
    std::cout<<"CPU visibility oracle: checked="<<checked<<" shadowed="<<shadowed<<" mismatch="<<mismatch<<'\n';
    require(checked>1000 && shadowed>100,"Oracle lacks lit/shadow coverage");require(mismatch==0,"Ray Query floor visibility differs from shared CPU triangle oracle");return {checked,shadowed};
}
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"Usage: IrisVulkanRayQueryAcceptance <runtime root> <new output directory>");
        const auto root=std::filesystem::absolute(argv[1]),output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(output);
        require(glfwInit()==GLFW_TRUE,"GLFW init failed");struct Lifetime{~Lifetime(){glfwTerminate();}} lifetime;
        auto floor=std::make_shared<ModelData>(makeGroundPlaneData());ObjLoader importer;
        auto cube=std::make_shared<ModelData>(importer.load(root/"assets/models/cube.obj").model);
        if(cube->materials.empty())cube->materials.emplace_back();
        for(auto& mesh:cube->meshes)for(auto& part:mesh.submeshes)if(part.materialIndex<0)part.materialIndex=0;
        pathtracer::SnapshotCamera camera;camera.position={6,6,9};camera.view=glm::lookAt(camera.position,glm::vec3(0,.5f,0),glm::vec3(0,1,0));camera.projection=glm::perspective(glm::radians(42.0f),16.0f/9,0.1f,100.0f);
        pathtracer::SceneSnapshotLighting lighting;lighting.directional.direction=glm::normalize(glm::vec3(.7f,-1,.3f));lighting.directional.radiance={3,3,3};lighting.environment.backgroundColor={.02f,.03f,.05f};
        auto snapshot=[&](float x=0,bool cast=true,bool empty=false){pathtracer::SceneSnapshotBuilder builder(camera,lighting);
            if(!empty){builder.addModel(floor,1,"Ground",glm::mat4(1));builder.addModel(cube,2,"Rigid A",glm::translate(glm::mat4(1),glm::vec3(x,1,0)),glm::vec3(.7f),cast);
                builder.addModel(cube,3,"Rigid B",glm::translate(glm::mat4(1),glm::vec3(-2,.5f,1))*glm::scale(glm::mat4(1),glm::vec3(.5f)),glm::vec3(.8f),cast);}return builder.finish();};
        unsigned checked=0,shadowed=0;std::uint64_t peak=0;std::vector<double> updateCpu,updateGpu;
        for(int round=0;round<2;++round){
            iris::vulkan::VulkanRaster renderer(root/"shaders/vulkan",true);
            const auto off=renderer.render(snapshot(),320,180,1,true);
            if(std::getenv("IRIS_VULKAN_DISABLE_RAY_QUERY")) {
                require(!renderer.statistics().rayQueryAvailable,"Forced missing capability ignored");bool refused=false;
                try{renderer.render(snapshot(),320,180,1,true,false,true);}catch(const std::exception& e){std::cout<<e.what()<<'\n';refused=true;}
                require(refused,"Missing capability did not refuse opt-in");require(off==renderer.render(snapshot(),320,180,1,true),"Missing capability refusal damaged unshadowed fallback");continue;
            }
#ifdef _WIN32
            RENDERDOC_API_1_0_0* captureApi=nullptr;
            if(std::getenv("IRIS_VULKAN_CAPTURE")){auto module=GetModuleHandleW(L"renderdoc.dll");require(module!=nullptr,"RenderDoc injection missing");auto get=reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module,"RENDERDOC_GetAPI"));require(get && get(eRENDERDOC_API_Version_1_0_0,reinterpret_cast<void**>(&captureApi))==1,"RenderDoc API missing");captureApi->StartFrameCapture(nullptr,nullptr);}
#endif
            const auto on=renderer.render(snapshot(),320,180,1,true,false,true);auto stats=renderer.statistics();
#ifdef _WIN32
            if(captureApi)require(captureApi->EndFrameCapture(nullptr,nullptr)==1,"Explicit Ray Query capture failed");
#endif
            require(stats.rayQueryAvailable && stats.blasCount==2 && stats.instanceCount==3 && stats.blasBuilds==2 && stats.tlasBuilds==1,"Unique-mesh BLAS / instance TLAS build contract failed");peak=std::max(peak,stats.accelerationBytes);
            require(difference(on,off)>300,"Sun shadow has no image contribution");
            const auto mask=renderer.render(snapshot(),320,180,1,true,false,true,true);
            png(output/("shadow-"+std::to_string(round)+".png"),on);png(output/("visibility-"+std::to_string(round)+".png"),mask);
            const auto counts=oracle(snapshot(),mask);checked=counts.first;shadowed=counts.second;
            require(renderer.statistics().blasBuilds==0 && renderer.statistics().tlasUpdates==0,"Unchanged scene rebuilt AS");
            const auto originalMeshes=cube->meshes;
            for(auto& mesh:cube->meshes)for(auto& vertex:mesh.vertices)vertex.position.x+=0.75f;
            const auto edited=renderer.render(snapshot(),320,180,1,true,false,true,true);
            require(difference(mask,edited)>100,"In-place mesh edit reused stale AS collection");
            require(renderer.statistics().blasBuilds==2 && renderer.statistics().tlasBuilds==1,"In-place mesh edit did not rebuild AS");
            oracle(snapshot(),edited);
            cube->meshes=originalMeshes;
            require(mask==renderer.render(snapshot(),320,180,1,true,false,true,true),"In-place mesh restore retained stale AS");
            require(difference(mask,renderer.render(snapshot(1),320,180,1,true,false,true,true))>100,"Rigid shadow failed to move");
            require(renderer.statistics().blasBuilds==0 && renderer.statistics().tlasUpdates==1,"Rigid movement must update TLAS without BLAS rebuild");
            require(mask==renderer.render(snapshot(),320,180,1,true,false,true,true),"Reset/rewind mask drift");
            require(renderer.render(snapshot(),640,360,1,true,false,true).size()==640U*360*4,"Ray Query resize failed");
            require(on==renderer.render(snapshot(),320,180,1,true,false,true),"Ray Query resize roundtrip drift");
            const auto noCaster=renderer.render(snapshot(0,false),320,180,1,true,false,true,true);require(difference(mask,noCaster)>100,"castsShadow ignored");
            cube->materials.front().alphaMode=MaterialAlphaMode::Mask;bool refused=false;
            try{renderer.render(snapshot(),320,180,1,true,false,true);}catch(const std::exception&){refused=true;}
            cube->materials.front().alphaMode=MaterialAlphaMode::Opaque;require(refused,"Alpha Mask accepted as opaque occluder");
            require(on==renderer.render(snapshot(),320,180,1,true,false,true),"Material failure recovery drift");
            renderer.render(snapshot(0,true,true),320,180,1,true,false,true);require(renderer.statistics().blasCount==0 && renderer.statistics().instanceCount==0,"Scene replacement retained stale occluders");
            require(on==renderer.render(snapshot(),320,180,1,true,false,true),"Scene reload drift");
            for(int i=0;i<12;++i){renderer.render(snapshot(i*.08f),320,180,1,true,false,true);stats=renderer.statistics();updateCpu.push_back(stats.accelerationCpuMilliseconds);updateGpu.push_back(stats.accelerationGpuMilliseconds);require(stats.blasBuilds==0,"Rigid sequence rebuilt BLAS");}
            require(off==renderer.render(snapshot(),320,180,1,true),"Disabled shadow baseline drift");require(renderer.statistics().accelerationBytes==0,"Disable retained acceleration memory");
            require(renderer.statistics().validationErrors==0 && renderer.statistics().validationWarnings==0,"Ray Query validation diagnostics");
            png(output/("shadow-"+std::to_string(round)+".png"),on);png(output/("off-"+std::to_string(round)+".png"),off);png(output/("visibility-"+std::to_string(round)+".png"),mask);
        }
        if(!updateCpu.empty()){std::sort(updateCpu.begin(),updateCpu.end());std::sort(updateGpu.begin(),updateGpu.end());}
        std::ofstream report(output/"report.json");report<<"{\"coreValidation\":true,\"synchronizationValidation\":true,\"warnings\":0,\"errors\":0,\"lifetimes\":2,\"oracleChecked\":"<<checked<<",\"oracleShadowed\":"<<shadowed<<",\"accelerationBytes\":"<<peak<<",\"updateCpuP50ms\":"<<(updateCpu.empty()?0:updateCpu[updateCpu.size()/2])<<",\"updateGpuP50ms\":"<<(updateGpu.empty()?0:updateGpu[updateGpu.size()/2])<<"}\n";report.close();require(static_cast<bool>(report),"Report write failed");
        std::cout<<"Vulkan Ray Query shadow / CPU oracle / BLAS reuse / TLAS update / lifecycle PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
