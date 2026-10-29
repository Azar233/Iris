#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include "vulkan/VulkanRaster.h"
#include "asset/InputManifest.h"
#include "pathtracer/SceneSnapshotCapture.h"
#include "io/AssimpImporter.h"
#include "render/Camera.h"
#include "render/Renderer.h"
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
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
std::size_t changed(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b) {
    require(a.size()==b.size(),"Image extent mismatch");std::size_t count=0;for(std::size_t i=0;i<a.size();++i)if(a[i]!=b[i])++count;return count;
}
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"Usage: IrisVulkanRasterAcceptance <runtime root> <output directory>");
        const std::filesystem::path root=std::filesystem::absolute(argv[1]),output=std::filesystem::absolute(argv[2]);
        require(glfwInit()==GLFW_TRUE,"GLFW init failed");
        struct GlfwLifetime {~GlfwLifetime(){glfwTerminate();}} glfwLifetime;
        AssimpImporter importer;auto model=std::make_shared<ModelData>(importer.load(root/"assets/models/pbr_material_test.gltf").model);
        require(!model->meshes.empty(),"Shared glTF importer produced no meshes");
        RendererSettings settings;settings.atmosphere.enabled=false;settings.iblEnabled=true;settings.skyboxEnabled=true;
        auto lighting=pathtracer::captureSceneLighting(settings,1.0f);
        Camera camera;auto orbit=camera.orbitState();orbit.target={0,0,0};orbit.distance=11;orbit.yawDegrees=12;orbit.pitchDegrees=8;camera.setOrbitState(orbit);
        auto snapshot=[&](float translation,bool lights=true,bool environment=true) {
            pathtracer::SnapshotCamera c;c.position=camera.position();c.view=camera.viewMatrix();c.projection=camera.projectionMatrix(16.0f/9.0f);
            auto l=lighting;if(!lights)l.directional.radiance={0,0,0};if(!environment){l.environment.radiancePixels.clear();l.environment.sourcePath.clear();l.environment.visibleToCamera=false;}
            pathtracer::SceneSnapshotBuilder builder(c,std::move(l));builder.addModel(model,1,"Shared glTF",glm::translate(glm::mat4(1),glm::vec3(translation,0,0)));return builder.finish();
        };
        std::filesystem::create_directories(output);
        std::vector<unsigned> frameCounts;std::vector<double> cpu,gpu;std::uint64_t peak=0;std::size_t lightDifference=0,environmentDifference=0,transformDifference=0;
        for(int lifetime=0;lifetime<2;++lifetime) {
            const auto shaders=output/("shaders-"+std::to_string(lifetime));std::filesystem::create_directories(shaders);
            for(const auto* file:{"raster.vert.spv","sky.vert.spv","raster.frag.spv"})std::filesystem::copy_file(root/"shaders/vulkan"/file,shaders/file,std::filesystem::copy_options::overwrite_existing);
            iris::vulkan::VulkanRaster renderer(shaders,true);
            unsigned frameCount=0;
            auto render=[&](const pathtracer::SceneSnapshot& value,int w,int h,float ev,bool tm,bool cull=false) {
                auto pixels=renderer.render(value,w,h,ev,tm,cull);++frameCount;return pixels;
            };
#ifdef _WIN32
            if(std::getenv("IRIS_VULKAN_CAPTURE")) {
                const auto module=GetModuleHandleW(L"renderdoc.dll");require(module!=nullptr,"RenderDoc not injected");
                auto get=reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module,"RENDERDOC_GetAPI"));RENDERDOC_API_1_0_0* api=nullptr;
                require(get && get(eRENDERDOC_API_Version_1_0_0,reinterpret_cast<void**>(&api))==1,"RenderDoc API unavailable");api->TriggerCapture();
            }
#endif
            const auto baseline=render(snapshot(0),320,180,1,true);
            require(renderer.statistics().validationEnabled && renderer.statistics().synchronizationValidation,"Strict validation not enabled");
            const auto uploadFrameBytes=renderer.statistics().allocatedBytes;
            peak=std::max(peak,uploadFrameBytes);
            require(baseline==render(snapshot(0),320,180,1,true),"Released staging changed the warm image");
            require(renderer.statistics().allocatedBytes<uploadFrameBytes,"Completed texture uploads remained resident");
            require(render(snapshot(0),640,360,1,true).size()==640U*360*4,"Resize output incorrect");
            require(renderer.statistics().pipelineCreations==0 && renderer.statistics().textureUploads==0 && renderer.statistics().textureDecodes==0,"Resize rebuilt persistent material/pipeline resources");
            peak=std::max(peak,renderer.statistics().allocatedBytes);
            require(baseline==render(snapshot(0),320,180,1,true),"Resize roundtrip drift");
            auto movedCamera=orbit;movedCamera.yawDegrees+=8;camera.setOrbitState(movedCamera);
            require(changed(baseline,render(snapshot(0),320,180,1,true))>100,"Camera change reused stale frame uniforms");
            require(renderer.statistics().pipelineCreations==0 && renderer.statistics().textureUploads==0 && renderer.statistics().textureDecodes==0 && renderer.statistics().hostUploadBytes<=1048576,"Camera change recreated static resources");
            camera.setOrbitState(orbit);
            require(baseline==render(snapshot(0),320,180,1,true),"Camera restore drift");
            lightDifference=changed(baseline,render(snapshot(0,false),320,180,1,true));require(lightDifference>100,"Directional light has no visible effect");
            environmentDifference=changed(baseline,render(snapshot(0,true,false),320,180,1,true));require(environmentDifference>100,"HDRI has no visible effect");
            require(baseline==render(snapshot(0),320,180,1,true),"Restore environment before isolated rigid-update check drifted");
            transformDifference=changed(baseline,render(snapshot(1),320,180,1,true));require(transformDifference>100,"Shared transform has no visible effect");
            require(renderer.statistics().pipelineCreations==0 && renderer.statistics().textureUploads==0 && renderer.statistics().textureDecodes==0,"Rigid move rebuilt persistent material/pipeline resources");
            require(baseline==render(snapshot(0),320,180,1,true),"Reset transform drift");
            const auto originalMode=model->materials.front().alphaMode;model->materials.front().alphaMode=MaterialAlphaMode::Blend;
            bool refused=false;try{render(snapshot(0),320,180,1,true);}catch(const std::exception&){refused=true;}
            model->materials.front().alphaMode=originalMode;require(refused,"Unsupported material was not rejected");
            require(baseline==render(snapshot(0),320,180,1,true),"Rejected input damaged renderer");
            {std::ofstream invalid(shaders/"raster.frag.spv",std::ios::binary);invalid<<"bad!";}
            refused=false;try{render(snapshot(0),320,180,1,true);}catch(const std::exception&){refused=true;}
            require(refused,"Broken shader reload must fail before submission");
            std::filesystem::copy_file(root/"shaders/vulkan/raster.frag.spv",shaders/"raster.frag.spv",std::filesystem::copy_options::overwrite_existing);
            require(baseline==render(snapshot(0),320,180,1,true),"Failed shader reload damaged recovery");
            const auto originalMeshes=model->meshes;
            for(auto& mesh:model->meshes)for(auto& vertex:mesh.vertices)vertex.position.x+=3.0f;
            require(changed(baseline,render(snapshot(0),320,180,1,true))>100,"In-place geometry edit reused stale uploaded vertices");
            model->meshes=originalMeshes;
            require(baseline==render(snapshot(0),320,180,1,true),"In-place geometry restore drift");
            for(auto& mesh:model->meshes)for(auto& vertex:mesh.vertices)vertex.normal=-vertex.normal;
            require(changed(baseline,render(snapshot(0),320,180,1,true))>100,"In-place normal edit reused stale uploaded vertices");
            model->meshes=originalMeshes;
            require(baseline==render(snapshot(0),320,180,1,true),"In-place normal restore drift");
            // The original fixture has no UV attribute and uniform MR texels.
            // Use distinct texels so this probe can detect stale uploaded UVs.
            const auto beforeUvModel=model;model=std::make_shared<ModelData>(*model);
            TextureData uvTexture;uvTexture.width=uvTexture.height=2;uvTexture.srgb=true;
            uvTexture.rgbaPixels={255,0,0,255, 255,0,0,255, 0,0,255,255, 0,0,255,255};model->textures={uvTexture};
            for(auto& material:model->materials){material.baseColorTextureIndex=0;material.normalTextureIndex=-1;material.metallicRoughnessTextureIndex=-1;}
            for(auto& mesh:model->meshes)for(auto& vertex:mesh.vertices)vertex.texCoord0=glm::vec2(0.25f,0.25f);
            const auto uvBaseline=render(snapshot(0),320,180,1,true);
            for(auto& mesh:model->meshes)for(auto& vertex:mesh.vertices)vertex.texCoord0=glm::vec2(0.75f,0.75f);
            require(changed(uvBaseline,render(snapshot(0),320,180,1,true))>100,"In-place UV edit reused stale uploaded vertices");
            model=beforeUvModel;
            require(baseline==render(snapshot(0),320,180,1,true),"In-place UV restore drift");
            require(!model->textures.empty(),"Texture failure probe lacks a source texture");
            const auto originalTexture=model->textures.front();
            model->textures.front().rgbaPixels.clear();model->textures.front().encodedData={0};model->textures.front().sourcePath.clear();
            refused=false;try{render(snapshot(0),320,180,1,true);}catch(const std::exception&){refused=true;}
            model->textures.front()=originalTexture;
            require(refused,"Invalid replacement texture was accepted");
            require(baseline==render(snapshot(0),320,180,1,true),"Failed texture replacement damaged cached scene recovery");
            const auto beforeFileModel=model;model=std::make_shared<ModelData>(*model);
            const auto fileTexture=output/("file-texture-"+std::to_string(lifetime)+".bmp");
            auto writeFileTexture=[&](bool blue) {
                std::vector<unsigned char> texels(16,255);
                for(std::size_t i=0;i<texels.size();i+=4){texels[i]=blue?0:255;texels[i+1]=0;texels[i+2]=blue?255:0;}
                require(stbi_write_bmp(fileTexture.string().c_str(),2,2,4,texels.data())!=0,"Write file texture failed");
                std::ofstream padding(fileTexture,std::ios::binary|std::ios::app);const std::string bytes(2*1024*1024,'x');padding.write(bytes.data(),bytes.size());
                require(static_cast<bool>(padding),"Write file comparison padding failed");
            };
            writeFileTexture(false);const auto fileTime=std::filesystem::last_write_time(fileTexture);
            const auto fileSize=std::filesystem::file_size(fileTexture);
            TextureData external;external.sourcePath=fileTexture;model->textures={external};
            for(auto& material:model->materials){material.baseColorTextureIndex=0;material.normalTextureIndex=-1;material.metallicRoughnessTextureIndex=-1;}
            const auto redFile=render(snapshot(0),320,180,1,true);
            require(redFile==render(snapshot(0),320,180,1,true),"Warm file texture drift");
            require(renderer.statistics().fileContentReadBytes>=fileSize && renderer.statistics().fileContentHashBytes==0,"Warm file check did not compare bytes without hashing");
#ifdef _WIN32
            auto requireFileClosed=[&] {
                const auto exclusive=CreateFileW(fileTexture.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                require(exclusive!=INVALID_HANDLE_VALUE,"File snapshot retained a handle after returning");
                require(CloseHandle(exclusive)!=0,"Close exclusive file probe failed");
            };
            requireFileClosed();
#endif
            // Change only the final byte after two identical 1 MiB blocks.
            {std::fstream edit(fileTexture,std::ios::binary|std::ios::in|std::ios::out);edit.seekp(-1,std::ios::end);edit.put('y');require(static_cast<bool>(edit),"Edit file suffix failed");}
            std::filesystem::last_write_time(fileTexture,fileTime);
            require(redFile==render(snapshot(0),320,180,1,true),"Ignored BMP padding changed rendered texels");
            require(renderer.statistics().fileContentHashBytes>=fileSize && renderer.statistics().textureDecodes>0,"Same-size same-time suffix edit escaped content checking");
            writeFileTexture(true);std::filesystem::last_write_time(fileTexture,fileTime);
            require(std::filesystem::file_size(fileTexture)==fileSize,"File edit probe changed size");
            require(changed(redFile,render(snapshot(0),320,180,1,true))>100,"Same-size same-time texture edit reused stale pixels");
            const auto replacement=output/("replacement-"+std::to_string(lifetime)+".bmp");
            std::filesystem::copy_file(fileTexture,replacement,std::filesystem::copy_options::overwrite_existing);
            std::filesystem::last_write_time(replacement,fileTime);
            writeFileTexture(false);std::filesystem::last_write_time(fileTexture,fileTime);
            require(redFile==render(snapshot(0),320,180,1,true),"Restore before file replacement drift");
#ifdef _WIN32
            require(MoveFileExW(replacement.c_str(),fileTexture.c_str(),MOVEFILE_REPLACE_EXISTING)!=0,"Open file handle prevented atomic replacement");
#else
            std::filesystem::remove(fileTexture);std::filesystem::rename(replacement,fileTexture);
#endif
            require(changed(redFile,render(snapshot(0),320,180,1,true))>100,"Same-path file replacement reused stale cached bytes");
            {
                capture::InputManifest inputs;render(snapshot(0),320,180,1,true);
                writeFileTexture(false);std::filesystem::last_write_time(fileTexture,fileTime);
                refused=false;try{render(snapshot(0),320,180,1,true);}catch(const std::exception& error){refused=std::string(error.what()).find("Capture input changed:")!=std::string::npos;}
                require(refused,"File-byte reuse bypassed active input capture");
            }
#ifdef _WIN32
            requireFileClosed();
#endif
            require(redFile==render(snapshot(0),320,180,1,true),"Capture failure poisoned file texture recovery");
            std::filesystem::resize_file(fileTexture,fileSize-65536);
            require(redFile==render(snapshot(0),320,180,1,true),"File prefix shrink lost valid BMP pixels");
            require(renderer.statistics().fileContentHashBytes>=fileSize-65536,"Shorter identical prefix reused stale file identity");
            std::filesystem::resize_file(fileTexture,0);
            refused=false;try{render(snapshot(0),320,180,1,true);}catch(const std::exception&){refused=true;}
            require(refused,"Empty replacement texture was accepted");
            std::filesystem::remove(fileTexture);
            refused=false;try{render(snapshot(0),320,180,1,true);}catch(const std::exception&){refused=true;}
            require(refused,"Missing replacement texture was accepted");
            writeFileTexture(false);std::filesystem::last_write_time(fileTexture,fileTime);
            require(redFile==render(snapshot(0),320,180,1,true),"File replacement failure damaged scene recovery");
            model=beforeFileModel;
            const auto savedEnvironment=lighting.environment;
            lighting.environment.sourcePath=fileTexture;lighting.environment.width=lighting.environment.height=0;
            lighting.environment.radiancePixels.clear();
            const auto fileEnvironment=render(snapshot(0),320,180,1,true);
            writeFileTexture(true);std::filesystem::last_write_time(fileTexture,fileTime);
            require(changed(fileEnvironment,render(snapshot(0),320,180,1,true))>100,"Same-size same-time environment file edit reused stale panorama");
            writeFileTexture(false);std::filesystem::last_write_time(fileTexture,fileTime);
            require(fileEnvironment==render(snapshot(0),320,180,1,true),"Environment file restore drift");
            lighting.environment.sourcePath=fileTexture;lighting.environment.width=lighting.environment.height=1;
            lighting.environment.radiancePixels={glm::vec3(4,0,0)};
            const auto redEnvironment=render(snapshot(0),320,180,1,true);
            lighting.environment.radiancePixels={glm::vec3(0,0,4)};
            const auto blueEnvironment=render(snapshot(0),320,180,1,true);
            require(changed(redEnvironment,blueEnvironment)>100,"Inline environment edit with a source path reused stale panorama");
            lighting.environment.sourcePath.clear();
            require(blueEnvironment==render(snapshot(0),320,180,1,true),"Environment source priority differs from shared EnvironmentLight");
            require(renderer.statistics().fileContentCacheBytes==0,"Unused file content remained after scene input change");
            lighting.environment=savedEnvironment;
            require(baseline==render(snapshot(0),320,180,1,true),"File/environment probe restore drift");
            const auto originalModel=model;
            model=std::make_shared<ModelData>(importer.load(root/"assets/models/textured_triangle.gltf").model);
            const auto triangle=render(snapshot(0,true,false),320,180,1,true,true);
            model=std::make_shared<ModelData>();
            const auto empty=render(snapshot(0,true,false),320,180,1,true);
            model=originalModel;
            require(changed(triangle,empty)>100,"Front-facing shared glTF textured triangle was culled");
            require(baseline==render(snapshot(0),320,180,1,true),"Restored model scene drift before warm-frame checks");
            for(int i=0;i<8;++i) {require(baseline==render(snapshot(0),320,180,1,true),"Repeated frame drift");
                const auto& warm=renderer.statistics();
                require(warm.pipelineCreations==0 && warm.textureDecodes==0 && warm.textureUploads==0,"Warm frame recreated static resources");
                require(warm.hostUploadBytes>0 && warm.hostUploadBytes<=1048576,"Warm frame reuploaded static geometry");
                cpu.push_back(renderer.statistics().cpuMilliseconds);gpu.push_back(renderer.statistics().gpuMilliseconds);peak=std::max(peak,renderer.statistics().allocatedBytes);}
            // A minified linear checker must converge to its mean, rather than
            // alias into bright/dark speckles. This fails with level-zero-only sampling.
            model=std::make_shared<ModelData>(*originalModel);
            TextureData checker;checker.width=checker.height=64;checker.rgbaPixels.resize(64U*64*4);
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
                const auto i=(y*64+x)*4;const auto value=static_cast<unsigned char>(((x+y)&1)?254:0);
                checker.rgbaPixels[i]=checker.rgbaPixels[i+1]=checker.rgbaPixels[i+2]=value;checker.rgbaPixels[i+3]=255;
            }
            model->textures={checker};
            for(auto& mesh:model->meshes)for(auto& vertex:mesh.vertices)
                vertex.texCoord0=glm::vec2(vertex.position.x+0.37f*vertex.position.z,vertex.position.y+0.61f*vertex.position.z)*16.0f;
            for(auto& material:model->materials) {
                material.baseColorTextureIndex=0;material.normalTextureIndex=material.metallicRoughnessTextureIndex=-1;
                material.baseColorFactor=glm::vec4(1);material.metallicFactor=0;material.roughnessFactor=1;
                material.emissiveFactor=glm::vec3(0);material.alphaMode=MaterialAlphaMode::Opaque;
            }
            const auto minified=render(snapshot(0),320,180,1,true);
            for(std::size_t i=0;i<checker.rgbaPixels.size();i+=4)checker.rgbaPixels[i]=checker.rgbaPixels[i+1]=checker.rgbaPixels[i+2]=127;
            model->textures={checker};const auto mean=render(snapshot(0),320,180,1,true);
            for(std::size_t i=0;i<mean.size();++i)require(std::abs(int(mean[i])-int(minified[i]))<=2,"Minified checker differs from linear mean");
            for(std::size_t i=3;i<checker.rgbaPixels.size();i+=4)checker.rgbaPixels[i]=0;
            model->textures={checker};for(auto& material:model->materials)material.alphaMode=MaterialAlphaMode::Mask;
            const auto masked=render(snapshot(0),320,180,1,true);
            model=std::make_shared<ModelData>();const auto background=render(snapshot(0),320,180,1,true);
            require(masked==background && changed(mean,masked)>100,"Mipmap upload lost Alpha Mask rejection");
            model=originalModel;
            require(renderer.statistics().validationErrors==0 && renderer.statistics().validationWarnings==0,"Vulkan raster validation diagnostics");
            frameCounts.push_back(frameCount);
            require(stbi_write_png((output/("lifetime-"+std::to_string(lifetime)+".png")).string().c_str(),320,180,4,baseline.data(),320*4)!=0,"Write acceptance image failed");
        }
        std::sort(cpu.begin(),cpu.end());std::sort(gpu.begin(),gpu.end());
        std::ofstream report(output/"report.json");report<<"{\"coreValidation\":true,\"synchronizationValidation\":true,\"lifetimes\":2,\"framesPerLifetime\":"<<frameCounts.front()<<",\"warnings\":0,\"errors\":0,\"cpuP50ms\":"<<cpu[cpu.size()/2]<<",\"cpuP95ms\":"<<cpu.back()<<",\"gpuP50ms\":"<<gpu[gpu.size()/2]<<",\"gpuP95ms\":"<<gpu.back()<<",\"peakAllocatedBytes\":"<<peak<<",\"lightChangedChannels\":"<<lightDifference<<",\"environmentChangedChannels\":"<<environmentDifference<<",\"transformChangedChannels\":"<<transformDifference<<"}\n";
        report.close();require(static_cast<bool>(report),"Write acceptance report failed");
        std::cout<<"Vulkan shared glTF/PBR/HDRI/transform/resize/reject/repeated lifetime PASS\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}


