namespace iris::vulkan {
namespace detail {
struct PtCapture {
#ifdef _WIN32
    RENDERDOC_API_1_0_0* api{};
    void begin() {
        if (!std::getenv("IRIS_VULKAN_CAPTURE")) return;
        const auto module = GetModuleHandleW(L"renderdoc.dll");
        if (!module) throw std::runtime_error("RenderDoc injection missing");
        const auto address = GetProcAddress(module, "RENDERDOC_GetAPI");
        pRENDERDOC_GetAPI get{};
        static_assert(sizeof(get) == sizeof(address));
        std::memcpy(&get, &address, sizeof(get));
        if (!get || !get(eRENDERDOC_API_Version_1_0_0, reinterpret_cast<void**>(&api)))
            throw std::runtime_error("RenderDoc API missing");
        api->StartFrameCapture(nullptr, nullptr);
    }
    void finish() {
        if (!api) return;
        const auto result = api->EndFrameCapture(nullptr, nullptr);
        api = nullptr;
        if (!result) throw std::runtime_error("GPU PT RenderDoc capture failed");
    }
    ~PtCapture() { if (api) api->EndFrameCapture(nullptr, nullptr); }
#else
    void begin() {}
    void finish() {}
#endif
};
struct PtTriangle {
    glm::vec4 p0,p1,p2,n0,n1,n2,base,emission,parameters;
    glm::vec4 uv0,uv1,uv2,tangent0,tangent1,tangent2;
    glm::ivec4 textures;
    glm::vec4 transmission,attenuation;
    glm::uvec4 identity;
};
struct PtVertex {glm::vec4 position,normal,uv,tangent;};
struct PtMaterial {
    glm::vec4 base,emission,parameters;
    glm::ivec4 textures;
    glm::vec4 transmission,attenuation;
    glm::uvec4 identity;
};
struct PtGeometry {
    std::vector<PtVertex> vertices;
    std::vector<glm::uvec4> triangles;
    std::vector<PtMaterial> materials;
    std::vector<glm::uvec4> instances,emitters;
};
struct PtPixel {glm::vec4 sum,albedo,normal,depth,direct,indirect,moments;glm::uvec4 count;};
struct PtFrame {
    glm::mat4 inverseView;
    glm::vec4 eyeScale;
    glm::uvec4 extent,options,counts;
    glm::vec4 background,sunDirection,sunRadiance;
    glm::vec4 localPosition[16]{},localDirection[16]{},localRadiance[16]{};
};
static_assert(sizeof(PtTriangle)==304 && sizeof(PtVertex)==64 && sizeof(PtMaterial)==112 && sizeof(glm::uvec4)==16 && sizeof(PtPixel)==128,"GPU PT std430 layout");
}
#include "VulkanTemporalGuides.inc"
#include "VulkanSpatialDenoise.inc"
#include "VulkanTemporalDenoise.inc"
struct VulkanPathTracer::Impl {
    detail::Bootstrap b;
    detail::PtCapture gpuCapture;
    std::unique_ptr<detail::RayQueryScene> acceleration;
    std::unique_ptr<detail::RasterResources> resources;
    std::unique_ptr<detail::RasterResources> imageResources;
    detail::RasterResources::Buffer output, previewBuffer, geometryBuffer, vertexBuffer, materialBuffer, frameBuffer, instanceBuffer;
    std::unique_ptr<detail::PtGuides> guides;
    std::unique_ptr<detail::PtSpatial> spatial;
    bool spatialEnabled{false};
    bool temporalEnabled{false};
    std::unique_ptr<detail::PtTemporal> temporal;
    std::uint64_t rawGeneration{1};
    int temporalFrame{0},temporalFps{30};
    bool temporalCancelled{false};
    detail::PtFrame uniform{};
    std::vector<std::vector<int>> textureIds;
    std::vector<glm::uvec4> emitterIdentities;
    std::filesystem::path shaderDirectory;
    std::string resourceIdentity;
    std::size_t maximumPixels{0};
    std::vector<detail::PtPixel> readbackPixels;
    VkDescriptorSet descriptors{};
    VkStridedDeviceAddressRegionKHR raygen{},miss{},hit{},callable{};
    pathtracer::SceneSnapshot sceneOwners; // Keep immutable model identities alive while accumulating.
    pathtracer::RenderSettings settings;
    pathtracer::RenderImage image;
    RasterStatistics stats;
    std::vector<unsigned char> previewRgba;
    bool indexedGeometry{false};
    bool usable{true};
    bool pending{false}, pendingDisplayOnly{false}, rawDirty{false}, displayDirty{true}, denoiseDirty{true};
    float exposure{1}, submittedExposure{1};bool toneMapping{true}, submittedToneMapping{true};
    std::chrono::steady_clock::time_point submitted;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    Impl(const std::filesystem::path& directory,const pathtracer::SceneSnapshot& scene,pathtracer::RenderSettings options,bool validation,bool manageGlfw):shaderDirectory(directory),sceneOwners(scene),settings(options) {
        using namespace detail;
        if(!settings.width || !settings.height || settings.width>pathtracer::gpuMaximumDimension || settings.height>pathtracer::gpuMaximumDimension || !settings.samplesPerPixel || settings.samplesPerPixel>65536 || !settings.maxDepth || settings.maxDepth>16)
            throw std::runtime_error("GPU PT requires resolution 1..1280, SPP 1..65536 and Depth 1..16");
        if(settings.adaptiveSampling || settings.ggxSamplingStrategy!=pathtracer::GgxSamplingStrategy::Distribution)
            throw std::runtime_error("GPU PT supports uniform SPP and GGX Distribution sampling only");
        if(scene.lighting().localLights.size()>16)throw std::runtime_error("GPU PT supports at most 16 local lights");
        const auto& camera=scene.camera();
        if(!std::isfinite(camera.verticalFieldOfViewRadians) || camera.verticalFieldOfViewRadians<=0 || camera.verticalFieldOfViewRadians>=3.14159265f || std::abs(glm::determinant(camera.view))<1e-12f)
            throw std::runtime_error("GPU PT requires a finite perspective camera");
        for(int column=0;column<4;++column)for(int row=0;row<4;++row)if(!std::isfinite(camera.view[column][row]))throw std::runtime_error("Nonfinite GPU PT camera matrix");
        for(int c=0;c<3;++c)if(!std::isfinite(camera.position[c]))throw std::runtime_error("Nonfinite GPU PT camera position");
        // One decoded base level, shared with CPU; half storage bounds upload size.
        std::vector<glm::uvec2> texels;
        std::vector<glm::uvec4> textureInfo;
        textureIds.resize(scene.assets().size());
        for(std::size_t asset=0;asset<scene.assets().size();++asset){
            for(const auto& source:scene.assets()[asset].model->textures){
                if(!source.sourcePath.empty())capture::recordInput(source.sourcePath);
                const pathtracer::CpuTexture texture(source);
                if(!texture.valid())throw std::runtime_error("GPU PT cannot decode material texture");
                if(texels.size()+texture.texels().size()>std::numeric_limits<unsigned>::max())throw std::runtime_error("GPU PT texture atlas overflow");
                textureIds[asset].push_back(static_cast<int>(textureInfo.size()));
                textureInfo.emplace_back(static_cast<unsigned>(texels.size()),texture.width(),texture.height(),0);
                for(const auto& value:texture.texels())texels.emplace_back(glm::packHalf2x16(glm::vec2(value)),glm::packHalf2x16(glm::vec2(value.z,value.w)));
            }
        }
        auto geometry=collectGeometry(scene,textureIds);
        const auto& triangles=geometry.triangles;const auto& instances=geometry.instances;
        auto& emitters=geometry.emitters;emitterIdentities=emitters;
        const auto indexedBytes=geometry.vertices.size()*sizeof(PtVertex)+triangles.size()*sizeof(glm::uvec4)+geometry.materials.size()*sizeof(PtMaterial);
        const auto expandedBytes=triangles.size()*sizeof(PtTriangle);
        // Small tables did not benefit from indirection in the fixed GPU test.
        // Retain the original emitter arithmetic/program and its exact outputs.
        indexedGeometry=expandedBytes>=64ULL*1024*1024 && indexedBytes<expandedBytes && emitters.empty();
        std::vector<PtTriangle> expanded;if(!indexedGeometry)expanded=expandTriangles(geometry);
        b.initialize(validation,validation,manageGlfw);
        if(!b.rayTracingPipelineAvailable)throw std::runtime_error("VK_KHR_ray_tracing_pipeline unavailable; select CPU Path Traced or Raster");
        // The device must exist before capture starts; keep it alive through EndFrameCapture.
        gpuCapture.begin();
        stats.device=b.report.deviceName;
        auto properties=info<VkPhysicalDeviceRayTracingPipelinePropertiesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR);
        auto physical=info<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);physical.pNext=&properties;b.f.vkGetPhysicalDeviceProperties2(b.physical,&physical);
        const auto pixelCount=static_cast<std::size_t>(settings.width)*settings.height;
        if(pixelCount>properties.maxRayDispatchInvocationCount)throw std::runtime_error("GPU PT dispatch "+std::to_string(settings.width)+"x"+std::to_string(settings.height)+" exceeds device invocation limit "+std::to_string(properties.maxRayDispatchInvocationCount));
        const auto maxStorage=physical.properties.limits.maxStorageBufferRange;
        maximumPixels=std::min<std::size_t>(properties.maxRayDispatchInvocationCount,maxStorage/sizeof(PtPixel));
        if(pixelCount>maxStorage/sizeof(PtPixel))throw std::runtime_error("GPU PT image "+std::to_string(settings.width)+"x"+std::to_string(settings.height)+" requires "+std::to_string(pixelCount*sizeof(PtPixel))+" Raw bytes; device storage-buffer limit is "+std::to_string(maxStorage));
        const unsigned storageDescriptors=indexedGeometry?10:8,bindingCount=indexedGeometry?13:11;
        if(physical.properties.limits.maxPerStageDescriptorStorageBuffers<storageDescriptors || physical.properties.limits.maxDescriptorSetStorageBuffers<storageDescriptors)
            throw std::runtime_error("GPU PT geometry exceeds device storage-buffer descriptor limits");
        if((indexedGeometry?(triangles.size()>maxStorage/sizeof(glm::uvec4) || geometry.vertices.size()>maxStorage/sizeof(PtVertex) || geometry.materials.size()>maxStorage/sizeof(PtMaterial)):expanded.size()>maxStorage/sizeof(PtTriangle)) || instances.size()>maxStorage/sizeof(glm::uvec4) || texels.size()>maxStorage/sizeof(glm::uvec2) || textureInfo.size()>maxStorage/sizeof(glm::uvec4) || emitters.size()>maxStorage/sizeof(glm::uvec4))
            throw std::runtime_error("GPU PT scene or image exceeds the device storage-buffer range");
        RayQueryScene::prepare(b,scene,acceleration,stats,true);
        resources=std::make_unique<RasterResources>(b);auto& r=*resources;
        std::array<VkDescriptorSetLayoutBinding,13> bindings{};
        for(unsigned i=0;i<bindingCount;++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            bindings[i].descriptorType=i==0?VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:i==4?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==5?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;}
        auto set=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);set.bindingCount=bindingCount;set.pBindings=bindings.data();check(b.f.vkCreateDescriptorSetLayout(b.device,&set,nullptr,&r.setLayout),"Create PT descriptors");
        VkPushConstantRange push{VK_SHADER_STAGE_RAYGEN_BIT_KHR,0,16};auto layout=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        layout.setLayoutCount=1;layout.pSetLayouts=&r.setLayout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;check(b.f.vkCreatePipelineLayout(b.device,&layout,nullptr,&r.layout),"Create PT layout");
        std::array<VkPipelineShaderStageCreateInfo,3> stages{};const char* files[]{raygenProgram(),"pt.rmiss.spv","pt.rchit.spv"};
        const VkShaderStageFlagBits stageFlags[]{VK_SHADER_STAGE_RAYGEN_BIT_KHR,VK_SHADER_STAGE_MISS_BIT_KHR,VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
        std::array<VkRayTracingShaderGroupCreateInfoKHR,3> groups{};
        for(unsigned i=0;i<3;++i){stages[i]=info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);stages[i].stage=stageFlags[i];stages[i].module=r.shader(directory/files[i]);stages[i].pName="main";
            groups[i]=info<VkRayTracingShaderGroupCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);groups[i].type=i<2?VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            groups[i].generalShader=i<2?i:VK_SHADER_UNUSED_KHR;groups[i].closestHitShader=i==2?2:VK_SHADER_UNUSED_KHR;groups[i].anyHitShader=groups[i].intersectionShader=VK_SHADER_UNUSED_KHR;}
        auto pipeline=info<VkRayTracingPipelineCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR);pipeline.stageCount=3;pipeline.pStages=stages.data();pipeline.groupCount=3;pipeline.pGroups=groups.data();pipeline.maxPipelineRayRecursionDepth=1;pipeline.layout=r.layout;
        check(b.f.vkCreateRayTracingPipelinesKHR(b.device,VK_NULL_HANDLE,VK_NULL_HANDLE,1,&pipeline,nullptr,&r.pipeline),"Create ray tracing pipeline");
        const auto align=[](VkDeviceSize value,VkDeviceSize boundary){return (value+boundary-1)&~(boundary-1);};
        const auto stride=align(properties.shaderGroupHandleSize,std::max(properties.shaderGroupBaseAlignment,properties.shaderGroupHandleAlignment));
        if(stride>properties.maxShaderGroupStride)throw std::runtime_error("GPU PT SBT stride exceeds device limit");
        std::vector<unsigned char> handles(3*properties.shaderGroupHandleSize);check(b.f.vkGetRayTracingShaderGroupHandlesKHR(b.device,r.pipeline,0,3,handles.size(),handles.data()),"Read shader group handles");
        const auto sbt=r.buffer(stride*3+properties.shaderGroupBaseAlignment,VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        const auto sbtAddress=acceleration->address(sbt),baseAddress=align(sbtAddress,properties.shaderGroupBaseAlignment);
        void* mapped{};check(b.f.vkMapMemory(b.device,sbt.memory,0,sbt.size,0,&mapped),"Map SBT");
        for(unsigned i=0;i<3;++i)std::memcpy(static_cast<unsigned char*>(mapped)+(baseAddress-sbtAddress)+stride*i,handles.data()+properties.shaderGroupHandleSize*i,properties.shaderGroupHandleSize);
        b.f.vkUnmapMemory(b.device,sbt.memory);raygen={baseAddress,stride,stride};miss={baseAddress+stride,stride,stride};hit={baseAddress+2*stride,stride,stride};
        if(indexedGeometry){
            geometryBuffer=r.buffer(triangles.size()*sizeof(glm::uvec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,triangles.data(),VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
            vertexBuffer=r.buffer(geometry.vertices.size()*sizeof(PtVertex),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,geometry.vertices.data(),VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
            materialBuffer=r.buffer(geometry.materials.size()*sizeof(PtMaterial),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,geometry.materials.data(),VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
        }else geometryBuffer=r.buffer(expanded.size()*sizeof(PtTriangle),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,expanded.data(),VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
        instanceBuffer=r.buffer(instances.size()*sizeof(glm::uvec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,instances.data(),VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
        imageResources=std::make_unique<RasterResources>(b);
        readbackPixels.resize(pixelCount);output=imageResources->buffer(pixelCount*sizeof(PtPixel),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,readbackPixels.data());
        std::vector<std::uint32_t> previewZeros(std::max<std::size_t>(pixelCount+1,4));
        previewBuffer=imageResources->buffer(previewZeros.size()*sizeof(std::uint32_t),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,previewZeros.data());
        previewRgba.resize(pixelCount*4);
        uniform.inverseView=glm::inverse(camera.view);uniform.eyeScale={camera.position,std::tan(camera.verticalFieldOfViewRadians*.5f)};
        uniform.extent={settings.width,settings.height,settings.maxDepth,settings.seed};const auto& light=scene.lighting();
        uniform.counts={static_cast<unsigned>(textureInfo.size()),static_cast<unsigned>(emitters.size()),0,0};
        uniform.options={settings.russianRouletteDepth,settings.nextEventEstimation?1U:0U,light.environment.visibleToCamera?1U:0U,static_cast<unsigned>(light.localLights.size())};
        uniform.background={light.environment.backgroundColor,light.environment.intensity};uniform.sunDirection={light.directional.direction,0};uniform.sunRadiance={light.directional.radiance,0};
        for(std::size_t i=0;i<light.localLights.size();++i){const auto& l=light.localLights[i];uniform.localPosition[i]={l.position,l.type==pathtracer::SnapshotLocalLightType::Spot?1.0f:0.0f};uniform.localDirection[i]={l.direction,l.radius};uniform.localRadiance[i]={l.radiance,l.outerConeCosine};}
        
        auto environmentSource=light.environment;environmentSource.intensity=1;
        if(!environmentSource.sourcePath.empty())capture::recordInput(environmentSource.sourcePath);
        const pathtracer::EnvironmentLight environment(environmentSource);
        if(!environmentSource.sourcePath.empty() && !environment.width())throw std::runtime_error("GPU PT cannot decode HDRI");
        const unsigned envWidth=environment.width()?environment.width():1,envHeight=environment.height()?environment.height():1;std::vector<glm::vec4> panorama;
        for(unsigned y=0;y<envHeight;++y)for(unsigned x=0;x<envWidth;++x){const float theta=3.14159265359f*(y+.5f)/envHeight,phi=6.28318530718f*((x+.5f)/envWidth-.5f);panorama.emplace_back(environment.radiance({std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi)}),1);}
        std::vector<glm::vec2> environmentDistribution;float previousCdf=0;
        if(environment.importanceSampled())for(const auto cumulative:environment.cumulativeWeights()){
            const auto cdf=static_cast<float>(cumulative/environment.cumulativeWeights().back());
            environmentDistribution.emplace_back(cdf,cdf-previousCdf);previousCdf=cdf;
        }
        uniform.counts.z=environment.importanceSampled()?1:0;
        if(environmentDistribution.empty())environmentDistribution.emplace_back(1,1);
        if(environmentDistribution.size()>maxStorage/sizeof(glm::vec2))throw std::runtime_error("GPU PT HDRI distribution exceeds storage range");
        const auto environmentBuffer=r.buffer(environmentDistribution.size()*sizeof(glm::vec2),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,environmentDistribution.data(),VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
        frameBuffer=r.buffer(sizeof(uniform),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,&uniform);
        auto command=b.frames[0].command;check(b.f.vkResetCommandBuffer(command,0),"Reset PT upload");auto begin=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);check(b.f.vkBeginCommandBuffer(command,&begin),"Begin PT upload");
        const auto env=r.texture(command,envWidth,envHeight,panorama,VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);check(b.f.vkEndCommandBuffer(command),"End PT upload");
        auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.commandBufferCount=1;submit.pCommandBuffers=&command;check(b.f.vkQueueSubmit(b.queue,1,&submit,VK_NULL_HANDLE),"Submit PT upload");check(b.f.vkDeviceWaitIdle(b.device),"Wait PT upload");
        auto sampler=info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;sampler.addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;check(b.f.vkCreateSampler(b.device,&sampler,nullptr,&r.sampler),"Create PT sampler");
        if (texels.empty()) texels.emplace_back(0, 0);
        if (textureInfo.empty()) textureInfo.emplace_back(0);
        if (emitters.empty()) emitters.emplace_back(0);
        const auto texelBuffer=r.buffer(texels.size()*sizeof(glm::uvec2),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,texels.data(),VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
        const auto textureBuffer=r.buffer(textureInfo.size()*sizeof(glm::uvec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,textureInfo.data(),VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
        const auto emitterBuffer=r.buffer(emitters.size()*sizeof(glm::uvec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,emitters.data(),VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
        const VkDescriptorPoolSize poolSizes[]{{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,storageDescriptors},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1}};
        auto pool=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool.maxSets=1;pool.poolSizeCount=4;pool.pPoolSizes=poolSizes;check(b.f.vkCreateDescriptorPool(b.device,&pool,nullptr,&r.descriptors),"Create PT descriptor pool");
        auto allocate=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);allocate.descriptorPool=r.descriptors;allocate.descriptorSetCount=1;allocate.pSetLayouts=&r.setLayout;check(b.f.vkAllocateDescriptorSets(b.device,&allocate,&descriptors),"Allocate PT set");
        VkDescriptorBufferInfo buffers[]{{geometryBuffer.handle,0,geometryBuffer.size},{instanceBuffer.handle,0,instanceBuffer.size},{output.handle,0,output.size},{frameBuffer.handle,0,frameBuffer.size},{texelBuffer.handle,0,texelBuffer.size},{textureBuffer.handle,0,textureBuffer.size},{emitterBuffer.handle,0,emitterBuffer.size},{environmentBuffer.handle,0,environmentBuffer.size},{previewBuffer.handle,0,previewBuffer.size},{vertexBuffer.handle,0,vertexBuffer.size},{materialBuffer.handle,0,materialBuffer.size}};
        VkDescriptorImageInfo imageInfo{r.sampler,env.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};auto as=info<VkWriteDescriptorSetAccelerationStructureKHR>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);as.accelerationStructureCount=1;as.pAccelerationStructures=&acceleration->tlas;
        std::array<VkWriteDescriptorSet,13> writes{};for(unsigned i=0;i<bindingCount;++i){writes[i]=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);writes[i].dstSet=descriptors;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;
            if(i==0)writes[i].pNext=&as;else if(i==5)writes[i].pImageInfo=&imageInfo;else writes[i].pBufferInfo=&buffers[i<5?i-1:i-2];}
        b.f.vkUpdateDescriptorSets(b.device,bindingCount,writes.data(),0,nullptr);
        auto query=info<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;check(b.f.vkCreateQueryPool(b.device,&query,nullptr,&r.queries),"Create PT timer");
        image.width=settings.width;image.height=settings.height;image.sum.resize(pixelCount);image.albedoSum.resize(pixelCount);image.normalSum.resize(pixelCount);image.depthSum.resize(pixelCount);image.primaryHitCount.resize(pixelCount);image.directSum.resize(pixelCount);image.indirectSum.resize(pixelCount);image.luminanceSum.resize(pixelCount);image.luminanceSquaredSum.resize(pixelCount);image.sampleCounts.resize(pixelCount);
        refreshMemoryStatistics();stats.triangles=static_cast<unsigned>(triangles.size());
        stats.readbackMemoryType=output.memoryType;stats.readbackMemoryFlags=output.memoryFlags;
        resourceIdentity=resourceKey(scene);
    }
    const char* raygenProgram()const{return indexedGeometry?"pt.indexed.rgen.spv":"pt.rgen.spv";}
    void refreshMemoryStatistics(){
        const auto* guide=guides?guides->resources.get():nullptr;
        const auto* filter=spatial?spatial->resources.get():nullptr;
        const auto* history=temporal?temporal->resources.get():nullptr;
        stats.guideReadbackBytes=guides?guides->readbackBytes:0;
        stats.allocatedBytes=resources->allocatedBytes+imageResources->allocatedBytes+acceleration->storage.allocatedBytes+(guide?guide->allocatedBytes:0)+(filter?filter->allocatedBytes:0)+(history?history->allocatedBytes:0);
        stats.deviceLocalBytes=resources->deviceLocalBytes+imageResources->deviceLocalBytes+acceleration->storage.deviceLocalBytes+(guide?guide->deviceLocalBytes:0)+(filter?filter->deviceLocalBytes:0)+(history?history->deviceLocalBytes:0);
    }
    static std::vector<detail::PtTriangle> expandTriangles(const detail::PtGeometry& geometry) {
        std::vector<detail::PtTriangle> result;result.reserve(geometry.triangles.size());
        for(const auto& primitive:geometry.triangles){const auto& m=geometry.materials[primitive.w];detail::PtTriangle t{};
            t.base=m.base;t.emission=m.emission;t.parameters=m.parameters;t.textures=m.textures;t.transmission=m.transmission;t.attenuation=m.attenuation;t.identity=m.identity;
            glm::vec4* positions[]{&t.p0,&t.p1,&t.p2};glm::vec4* normals[]{&t.n0,&t.n1,&t.n2};
            glm::vec4* uvs[]{&t.uv0,&t.uv1,&t.uv2};glm::vec4* tangents[]{&t.tangent0,&t.tangent1,&t.tangent2};
            for(int c=0;c<3;++c){const auto& v=geometry.vertices[primitive[c]];*positions[c]=v.position;*normals[c]=v.normal;*uvs[c]=v.uv;*tangents[c]=v.tangent;}
            result.push_back(t);
        }
        return result;
    }
    static detail::PtGeometry collectGeometry(const pathtracer::SceneSnapshot& scene,const std::vector<std::vector<int>>& textureIds) {
        using namespace detail;
        PtGeometry geometry;
        const auto tintLinear=[](glm::vec3 value){for(int i=0;i<3;++i){const float c=std::clamp(value[i],0.0f,1.0f);value[i]=c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);}return value;};
        std::size_t triangleCount=0,vertexCount=0,materialCount=0;
        // All table references are uint32 in GLSL. Check imported ranges and
        // aggregate capacity before allocating or narrowing any table index.
        const auto addCount=[](std::size_t& total,std::size_t count){
            if(count>std::numeric_limits<std::uint32_t>::max()-total)throw std::runtime_error("GPU PT indexed geometry exceeds uint32 capacity");
            total+=count;
        };
        for(const auto& instance:scene.instances()) {
            if(instance.skinned || !instance.transformInvertible)throw std::runtime_error("GPU PT rejects skinned or singular geometry");
            const auto& model=*scene.assets().at(instance.assetIndex).model;const auto& mesh=model.meshes.at(instance.meshIndex);
            addCount(vertexCount,mesh.vertices.size());
            for(const auto& part:mesh.submeshes) {
                if(part.firstIndex>mesh.indices.size() || part.indexCount>mesh.indices.size()-part.firstIndex || part.indexCount%3)throw std::runtime_error("Invalid GPU PT triangle range");
                if(part.materialIndex>=0 && model.materials.at(part.materialIndex).alphaMode!=MaterialAlphaMode::Opaque)
                    throw std::runtime_error("GPU PT rejects Alpha Mask/Blend; only opaque coverage is supported");
                addCount(triangleCount,part.indexCount/3);if(part.indexCount)addCount(materialCount,1);
            }
        }
        geometry.vertices.reserve(vertexCount);geometry.triangles.reserve(triangleCount);geometry.materials.reserve(materialCount);
        for(const auto& instance:scene.instances()) {
            const auto& model=*scene.assets().at(instance.assetIndex).model;const auto& mesh=model.meshes.at(instance.meshIndex);
            const auto first=static_cast<std::uint32_t>(geometry.triangles.size());
            const auto vertexOffset=static_cast<std::uint32_t>(geometry.vertices.size());
            const auto tangentToWorld=glm::mat3(instance.objectToWorld);
            const bool mirrored=glm::determinant(tangentToWorld)<0;
            for(const auto& vertex:mesh.vertices){
                PtVertex v{};v.position=instance.objectToWorld*glm::vec4(vertex.position,1);
                const auto n=instance.normalToWorld*vertex.normal;
                v.normal=glm::vec4(glm::dot(n,n)>1e-12f?glm::normalize(n):glm::vec3(0),0);
                v.uv=glm::vec4(vertex.texCoord0,0,0);const auto tangent=tangentToWorld*glm::vec3(vertex.tangent);
                v.tangent=glm::vec4(glm::dot(tangent,tangent)>1e-12f?glm::normalize(tangent):glm::vec3(0),vertex.tangent.w*(mirrored?-1.f:1.f));
                geometry.vertices.push_back(v);
            }
            const auto tint=tintLinear(instance.tint);
            for(const auto& part:mesh.submeshes) {
                MaterialData material;if(part.materialIndex>=0)material=model.materials.at(part.materialIndex);
                if(!part.indexCount)continue;
                PtMaterial m{};m.base=glm::vec4(glm::vec3(material.baseColorFactor)*tint,1);
                m.emission=glm::vec4(glm::max(material.emissiveFactor,glm::vec3(0)),0);
                m.parameters={material.metallicFactor,material.roughnessFactor,material.doubleSided?1.0f:0.0f,mirrored?1.0f:0.0f};
                const auto textureId=[&](int index){return index<0?-1:textureIds.at(instance.assetIndex).at(static_cast<std::size_t>(index));};
                m.textures={textureId(material.baseColorTextureIndex),textureId(material.metallicRoughnessTextureIndex),textureId(material.normalTextureIndex),textureId(material.thicknessTextureIndex)};
                m.transmission={material.transmissionFactor,material.indexOfRefraction,material.thicknessFactor,material.attenuationDistance};
                m.attenuation={material.attenuationColor,0};m.identity={instance.assetIndex,static_cast<unsigned>(part.materialIndex+1),0,0};
                const auto materialIndex=static_cast<std::uint32_t>(geometry.materials.size());geometry.materials.push_back(m);
                const bool emissive=glm::any(glm::greaterThan(glm::vec3(m.emission),glm::vec3(0)));
                for(std::size_t i=part.firstIndex;i<static_cast<std::size_t>(part.firstIndex)+part.indexCount;i+=3){
                    glm::uvec4 primitive(0,0,0,materialIndex);
                    for(int corner=0;corner<3;++corner){const auto index=mesh.indices.at(i+corner);
                        if(index>=mesh.vertices.size())throw std::runtime_error("Invalid GPU PT vertex index");
                        primitive[corner]=vertexOffset+index;}
                    if(mirrored)std::swap(primitive.y,primitive.z);
                    if(emissive){const auto& a=geometry.vertices[primitive.x].position;const auto& b=geometry.vertices[primitive.y].position;const auto& c=geometry.vertices[primitive.z].position;
                        const auto cross=glm::cross(glm::vec3(b-a),glm::vec3(c-a));const float area=.5f*glm::length(cross);
                        if(area>5e-9f)geometry.emitters.emplace_back(static_cast<unsigned>(geometry.triangles.size()),0,0,0);}
                    geometry.triangles.push_back(primitive);
                }
            }
            if(geometry.triangles.size()!=first)geometry.instances.emplace_back(first,static_cast<std::uint32_t>(geometry.triangles.size())-first,0,0);
        }
        return geometry;
    }
    std::string resourceKey(const pathtracer::SceneSnapshot& scene) {
        using namespace detail;
        RasterStatistics ignored;RasterFileInputs fileInputs;
        const auto assetKey=rasterAssetKey(scene,ignored,fileInputs);
        // Raster omits tangent and volume fields that this PT shader consumes.
        RasterFingerprint ptKey;ptKey.add(assetKey);
        for(const auto& asset:scene.assets()) {
            for(const auto& mesh:asset.model->meshes)for(const auto& vertex:mesh.vertices)ptKey.add(vertex.tangent);
            for(const auto& m:asset.model->materials){ptKey.add(m.indexOfRefraction);ptKey.add(m.thicknessFactor);ptKey.add(m.thicknessTextureIndex);ptKey.add(m.attenuationDistance);ptKey.add(m.attenuationColor);}
        }
        std::string bytes;const auto add=[&](const auto& v){bytes.append(reinterpret_cast<const char*>(&v),sizeof(v));};
        add(ptKey.value);
        const auto& l=scene.lighting();add(l.directional.direction);add(l.directional.radiance);
        add(l.environment.backgroundColor);add(l.environment.intensity);add(l.environment.visibleToCamera);
        add(l.environment.width);add(l.environment.height);
        for(const auto& p:l.environment.radiancePixels)add(p);
        bytes+=l.environment.sourcePath.generic_string();
        if(!l.environment.sourcePath.empty())bytes+=capture::snapshot(l.environment.sourcePath).fingerprint;
        add(l.localLights.size());for(const auto& light:l.localLights){add(light.type);add(light.position);add(light.direction);add(light.radiance);add(light.radius);add(light.outerConeCosine);}
        add(scene.instances().size());for(const auto& i:scene.instances()){add(i.entityId);add(i.assetIndex);add(i.meshIndex);add(i.tint);add(i.castsShadow);add(i.skinned);}
        for(const auto file:{raygenProgram(),"pt.rmiss.spv","pt.rchit.spv"})bytes+=capture::snapshot(shaderDirectory/file).fingerprint;
        return bytes;
    }
    void writeBuffer(const detail::RasterResources::Buffer& buffer,const void* data) {
        void* mapped{};detail::check(b.f.vkMapMemory(b.device,buffer.memory,0,buffer.size,0,&mapped),"Map PT restart buffer");
        std::memcpy(mapped,data,static_cast<std::size_t>(buffer.size));b.f.vkUnmapMemory(b.device,buffer.memory);
    }
    bool restart(const pathtracer::SceneSnapshot& scene,pathtracer::RenderSettings options) {
        using namespace detail;
        // Build and validate candidates before touching the live accumulation.
        if(!usable || b.errors.load()!=0 || resourceKey(scene)!=resourceIdentity)return false;
        if(!options.width || !options.height || options.width>pathtracer::gpuMaximumDimension || options.height>pathtracer::gpuMaximumDimension || !options.samplesPerPixel || options.samplesPerPixel>65536 || !options.maxDepth || options.maxDepth>16 || options.adaptiveSampling || options.ggxSamplingStrategy!=pathtracer::GgxSamplingStrategy::Distribution)
            throw std::runtime_error("Invalid GPU PT restart settings");
        const auto& camera=scene.camera();
        if(!std::isfinite(camera.verticalFieldOfViewRadians) || camera.verticalFieldOfViewRadians<=0 || camera.verticalFieldOfViewRadians>=3.14159265f || std::abs(glm::determinant(camera.view))<1e-12f)throw std::runtime_error("Invalid GPU PT restart camera");
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(camera.view[c][r]))throw std::runtime_error("Nonfinite GPU PT camera");
        for(int c=0;c<3;++c)if(!std::isfinite(camera.position[c]))throw std::runtime_error("Nonfinite GPU PT camera position");
        bool moved=scene.instances().size()!=sceneOwners.instances().size();
        for(std::size_t i=0;!moved && i<scene.instances().size();++i)moved=scene.instances()[i].objectToWorld!=sceneOwners.instances()[i].objectToWorld;
        PtGeometry geometry;std::vector<PtTriangle> expanded;
        if(moved){geometry=collectGeometry(scene,textureIds);
            if(geometry.emitters!=emitterIdentities)return false;
            if(indexedGeometry){
                if(geometry.triangles.size()*sizeof(glm::uvec4)!=geometryBuffer.size || geometry.vertices.size()*sizeof(PtVertex)!=vertexBuffer.size || geometry.materials.size()*sizeof(PtMaterial)!=materialBuffer.size)return false;
            }else{expanded=expandTriangles(geometry);if(expanded.size()*sizeof(PtTriangle)!=geometryBuffer.size)return false;}}
        const auto count=static_cast<std::size_t>(options.width)*options.height;
        if(count>maximumPixels)throw std::runtime_error("GPU PT resize "+std::to_string(options.width)+"x"+std::to_string(options.height)+" exceeds device dispatch/storage limit of "+std::to_string(maximumPixels)+" pixels");
        std::vector<PtPixel> zeros(count);
        pathtracer::RenderImage next;next.width=options.width;next.height=options.height;
        next.sum.resize(count);next.albedoSum.resize(count);next.normalSum.resize(count);next.depthSum.resize(count);next.primaryHitCount.resize(count);next.directSum.resize(count);next.indirectSum.resize(count);next.luminanceSum.resize(count);next.luminanceSquaredSum.resize(count);next.sampleCounts.resize(count);
        const bool resize=options.width!=settings.width || options.height!=settings.height;
        std::unique_ptr<RasterResources> candidate;RasterResources::Buffer nextOutput,nextPreview;
        std::vector<std::uint32_t> previewZeros(std::max<std::size_t>(count+1,4));
        if(resize){candidate=std::make_unique<RasterResources>(b);nextOutput=candidate->buffer(count*sizeof(PtPixel),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,zeros.data());nextPreview=candidate->buffer(previewZeros.size()*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,previewZeros.data());}
        // Publish a submitted old-camera sample before replacing its scene.
        // Otherwise continuous input can discard every completed sample forever.
        if(pending && !temporalCancelled){complete(false,true);if(spatialEnabled || temporalEnabled)resolveDenoise();}
        usable=false;
        check(b.f.vkDeviceWaitIdle(b.device),"Wait PT restart");
        pending=false;gpuCapture.finish();gpuCapture.begin();stats.rawReadbackBytes=stats.previewReadbackBytes=0;stats.previewDispatches=0;
        spatial.reset();if(guides)guides->readbackBytes=0;stats.spatialCpuMilliseconds=stats.spatialGpuMilliseconds=stats.guideGpuMilliseconds=stats.guideCpuMilliseconds=0;stats.spatialAllocatedBytes=stats.spatialReadbackBytes=0;stats.spatialDispatches=0;
        stats.blasBuilds=stats.tlasBuilds=stats.tlasUpdates=0;stats.accelerationCpuMilliseconds=stats.accelerationGpuMilliseconds=0;
        if(moved){RayQueryScene::prepare(b,scene,acceleration,stats,true);
            if(indexedGeometry){writeBuffer(geometryBuffer,geometry.triangles.data());writeBuffer(vertexBuffer,geometry.vertices.data());writeBuffer(materialBuffer,geometry.materials.data());}
            else writeBuffer(geometryBuffer,expanded.data());}
        if(resize){
            VkDescriptorBufferInfo buffer{nextOutput.handle,0,nextOutput.size};auto write=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            write.dstSet=descriptors;write.dstBinding=3;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffer;
            b.f.vkUpdateDescriptorSets(b.device,1,&write,0,nullptr);
            buffer={nextPreview.handle,0,nextPreview.size};write.dstBinding=10;b.f.vkUpdateDescriptorSets(b.device,1,&write,0,nullptr);
            imageResources=std::move(candidate);output=nextOutput;previewBuffer=nextPreview;
        }else {writeBuffer(output,zeros.data());writeBuffer(previewBuffer,previewZeros.data());}
        uniform.inverseView=glm::inverse(camera.view);uniform.eyeScale={camera.position,std::tan(camera.verticalFieldOfViewRadians*.5f)};
        uniform.extent={options.width,options.height,options.maxDepth,options.seed};uniform.options.x=options.russianRouletteDepth;uniform.options.y=options.nextEventEstimation?1U:0U;
        writeBuffer(frameBuffer,&uniform);
        if(moved || camera.view!=sceneOwners.camera().view || camera.position!=sceneOwners.camera().position)++rawGeneration;
        temporalCancelled=false;
        if(guides){guides->dirty=true;guides->cancelled=false;
            if(resize || options.seed!=settings.seed || options.maxDepth!=settings.maxDepth || options.samplesPerPixel!=settings.samplesPerPixel || options.russianRouletteDepth!=settings.russianRouletteDepth || options.nextEventEstimation!=settings.nextEventEstimation)guides->invalidate();}
        image=std::move(next);readbackPixels=std::move(zeros);
        if(resize)previewRgba.assign(count*4,0);
        rawDirty=false;displayDirty=true;denoiseDirty=true;settings=options;sceneOwners=scene;started=std::chrono::steady_clock::now();
        refreshMemoryStatistics();
        stats.readbackMemoryType=output.memoryType;stats.readbackMemoryFlags=output.memoryFlags;
        stats.dispatchBlasBuilds=stats.blasBuilds;stats.dispatchTlasBuilds=stats.tlasBuilds;stats.dispatchTlasUpdates=stats.tlasUpdates;
        usable=true;return true;
    }
    void readRaw() {
        using namespace detail;
        if(!usable)throw std::runtime_error("GPU PT state requires backend rebuild");
        if(!rawDirty)return;
        const auto rawStart=std::chrono::steady_clock::now();
        void* mapped{};check(b.f.vkMapMemory(b.device,output.memory,0,output.size,0,&mapped),"Read PT image");
        // Host-coherent memory need not be host-cached. Read it once sequentially,
        // then perform the repeated finite checks and AOV gathers in CPU memory.
        std::memcpy(readbackPixels.data(),mapped,static_cast<std::size_t>(output.size));
        b.f.vkUnmapMemory(b.device,output.memory);
        const auto* pixels=readbackPixels.data();
        bool finite=true;std::uint64_t pathRays=0,shadowRays=0;
        for(std::size_t i=0;i<image.sum.size();++i){const auto& p=pixels[i];for(int c=0;c<3;++c)finite&=std::isfinite(p.sum[c]) && std::isfinite(p.albedo[c]) && std::isfinite(p.normal[c]) && std::isfinite(p.direct[c]) && std::isfinite(p.indirect[c]);finite&=std::isfinite(p.depth.x) && std::isfinite(p.moments.x) && std::isfinite(p.moments.y);
            image.sum[i]=glm::vec3(p.sum);image.albedoSum[i]=glm::vec3(p.albedo);image.normalSum[i]=glm::vec3(p.normal);image.depthSum[i]=p.depth.x;image.directSum[i]=glm::vec3(p.direct);image.indirectSum[i]=glm::vec3(p.indirect);image.luminanceSum[i]=p.moments.x;image.luminanceSquaredSum[i]=p.moments.y;image.sampleCounts[i]=p.count.x;image.primaryHitCount[i]=p.count.y;pathRays+=p.count.z;shadowRays+=p.count.w;}
        if(!finite){usable=false;throw std::runtime_error("Nonfinite GPU PT radiance");}
        image.statistics.pathRays=pathRays;image.statistics.shadowRays=shadowRays;rawDirty=false;
        stats.rawReadbackBytes+=output.size;
        image.statistics.renderMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-rawStart).count();
    }
    void dispatch(bool displayOnly) {
        using namespace detail;
        if(image.completedSamples){stats.blasBuilds=stats.tlasBuilds=stats.tlasUpdates=0;stats.dispatchBlasBuilds=stats.dispatchTlasBuilds=stats.dispatchTlasUpdates=0;stats.accelerationCpuMilliseconds=stats.accelerationGpuMilliseconds=0;}
        submitted=std::chrono::steady_clock::now();auto& frame=b.frames[0];auto& r=*resources;
        check(b.f.vkWaitForFences(b.device,1,&frame.fence,VK_TRUE,UINT64_MAX),"Wait PT frame");
        // Each dispatch revalidates every pixel, including display-only resolves.
        // Clear only the diagnostic word so a corrected display can recover
        // without throwing away a valid Raw accumulation.
        void* status{};check(b.f.vkMapMemory(b.device,previewBuffer.memory,0,previewBuffer.size,0,&status),"Reset PT preview status");
        static_cast<std::uint32_t*>(status)[static_cast<std::size_t>(settings.width)*settings.height]=0;
        b.f.vkUnmapMemory(b.device,previewBuffer.memory);stats.hostUploadBytes=sizeof(std::uint32_t);
        check(b.f.vkResetCommandBuffer(frame.command,0),"Reset PT pass");
        auto begin=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);check(b.f.vkBeginCommandBuffer(frame.command,&begin),"Begin PT pass");
        b.f.vkCmdResetQueryPool(frame.command,r.queries,0,2);b.f.vkCmdWriteTimestamp(frame.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,r.queries,0);
        auto memory=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);memory.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_HOST_READ_BIT|VK_ACCESS_HOST_WRITE_BIT;memory.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        b.f.vkCmdPipelineBarrier(frame.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,0,1,&memory,0,nullptr,0,nullptr);
        if(b.f.vkCmdBeginDebugUtilsLabelEXT){auto label=info<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);label.pLabelName=displayOnly?"Iris.GPUPathTracer.Resolve":"Iris.GPUPathTracer.Sample";b.f.vkCmdBeginDebugUtilsLabelEXT(frame.command,&label);}
        b.f.vkCmdBindPipeline(frame.command,VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,r.pipeline);b.f.vkCmdBindDescriptorSets(frame.command,VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,r.layout,0,1,&descriptors,0,nullptr);
        struct Push {std::uint32_t sample;float exposure;std::uint32_t tone,displayOnly;};
        const Push push{image.completedSamples,exposure,toneMapping?1U:0U,displayOnly?1U:0U};
        b.f.vkCmdPushConstants(frame.command,r.layout,VK_SHADER_STAGE_RAYGEN_BIT_KHR,0,sizeof(push),&push);b.f.vkCmdTraceRaysKHR(frame.command,&raygen,&miss,&hit,&callable,settings.width,settings.height,1);
        if(b.f.vkCmdEndDebugUtilsLabelEXT)b.f.vkCmdEndDebugUtilsLabelEXT(frame.command);
        memory.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;memory.dstAccessMask=VK_ACCESS_HOST_READ_BIT;b.f.vkCmdPipelineBarrier(frame.command,VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,VK_PIPELINE_STAGE_HOST_BIT,0,1,&memory,0,nullptr,0,nullptr);
        b.f.vkCmdWriteTimestamp(frame.command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,r.queries,1);check(b.f.vkEndCommandBuffer(frame.command),"End PT pass");
        auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.commandBufferCount=1;submit.pCommandBuffers=&frame.command;usable=false;check(b.f.vkResetFences(b.device,1,&frame.fence),"Reset PT fence");check(b.f.vkQueueSubmit(b.queue,1,&submit,frame.fence),"Dispatch PT");usable=true;
        ++stats.previewDispatches;pending=true;pendingDisplayOnly=displayOnly;submittedExposure=exposure;submittedToneMapping=toneMapping;
    }
    bool complete(bool raw,bool wait) {
        using namespace detail;
        if(!pending)return false;
        auto& frame=b.frames[0];
        if(wait)check(b.f.vkWaitForFences(b.device,1,&frame.fence,VK_TRUE,UINT64_MAX),"Wait PT readback");
        else {const auto state=b.f.vkGetFenceStatus(b.device,frame.fence);if(state==VK_NOT_READY)return false;check(state,"Poll PT completion");}
        const auto readbackStart=std::chrono::steady_clock::now();
        stats.submitWaitMilliseconds=std::chrono::duration<double,std::milli>(readbackStart-submitted).count();
        void* mapped{};check(b.f.vkMapMemory(b.device,previewBuffer.memory,0,previewBuffer.size,0,&mapped),"Read PT preview");
        const auto error=static_cast<const std::uint32_t*>(mapped)[static_cast<std::size_t>(settings.width)*settings.height];
        if(!error)std::memcpy(previewRgba.data(),mapped,previewRgba.size());
        b.f.vkUnmapMemory(b.device,previewBuffer.memory);
        pending=false;denoiseDirty=true;
        if(error&1U){usable=false;throw std::runtime_error("Nonfinite GPU PT Raw/AOV accumulation");}
        // A display error must not lose a valid sample that the GPU already stored.
        // Keep the CPU sample index aligned with the accumulation before reporting it.
        if(!pendingDisplayOnly){++image.completedSamples;rawDirty=true;}
        if(!pendingDisplayOnly)image.statistics.renderMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        if(raw)readRaw();
        const auto previewBytes=error?sizeof(std::uint32_t):previewBuffer.size;
        stats.previewReadbackBytes+=previewBytes;
        stats.readbackBytes=previewBytes+(raw?output.size:0);
        stats.readbackMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-readbackStart).count();
        displayDirty=(error&2U)!=0 || submittedExposure!=exposure || submittedToneMapping!=toneMapping;
        // A denoised capture ends after its guides and compute resolves, not
        // at the earlier Raw readback boundary.
        if(!spatialEnabled && !temporalEnabled)gpuCapture.finish();
        image.statistics.cameraSamples=static_cast<std::uint64_t>(settings.width)*settings.height*image.completedSamples;image.statistics.workerCount=0;image.statistics.estimatedAccelerationBytes=stats.accelerationBytes;
        std::uint64_t ticks[2]{};check(b.f.vkGetQueryPoolResults(b.device,resources->queries,0,2,sizeof(ticks),ticks,sizeof(ticks[0]),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"Read PT timestamps");VkPhysicalDeviceProperties properties{};b.f.vkGetPhysicalDeviceProperties(b.physical,&properties);
        const auto mask=acceleration->timestampBits==64?UINT64_MAX:((1ULL<<acceleration->timestampBits)-1);if(!pendingDisplayOnly)stats.gpuMilliseconds=static_cast<double>((ticks[1]-ticks[0])&mask)*properties.limits.timestampPeriod/1e6;
        stats.cpuMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-submitted).count();stats.validationEnabled=b.report.validationEnabled;stats.synchronizationValidation=b.report.synchronizationValidation;stats.validationWarnings=b.warnings.load();stats.validationErrors=b.errors.load();
        if(!pendingDisplayOnly){++stats.completedSampleDispatches;stats.completedSampleGpuMilliseconds=stats.gpuMilliseconds+stats.accelerationGpuMilliseconds;}
        if(stats.validationErrors){usable=false;throw std::runtime_error("GPU PT validation errors");}
        if(error&2U)throw std::runtime_error("Invalid GPU PT display; Raw accumulation retained");
        return true;
    }
    bool pass(const std::atomic<bool>* cancel,bool raw) {
        if(!usable)throw std::runtime_error("GPU PT state requires backend rebuild");
        if(cancel && cancel->load()){temporalCancelled=true;if(guides){guides->invalidate();guides->cancelled=true;}return false;}
        if(image.completedSamples>=settings.samplesPerPixel)return false;
        if(!pending)dispatch(false);
        return complete(raw,true);
    }
    bool preview(float value,bool tone,bool wait) {
        if(!usable)throw std::runtime_error("GPU PT state requires backend rebuild");
        stats.hostUploadBytes=stats.readbackBytes=0;stats.readbackMilliseconds=stats.submitWaitMilliseconds=0;
        if(pending || image.completedSamples){
            stats.dispatchBlasBuilds=stats.dispatchTlasBuilds=stats.dispatchTlasUpdates=0;
            stats.accelerationCpuMilliseconds=stats.accelerationGpuMilliseconds=0;
        }
        if(!std::isfinite(value) || value<0)throw std::runtime_error("Invalid GPU PT display exposure");
        if(value!=exposure || tone!=toneMapping){exposure=value;toneMapping=tone;displayDirty=true;}
        if(pending){const bool done=complete(false,wait);if(done && (spatialEnabled || temporalEnabled))resolveDenoise();return done;}
        if(image.completedSamples>=settings.samplesPerPixel && !displayDirty)return false;
        dispatch(image.completedSamples>=settings.samplesPerPixel);
        const bool done=complete(false,wait);if(done && (spatialEnabled || temporalEnabled))resolveDenoise();return done;
    }
    void resolveDenoise() {
        using namespace detail;
        if(!usable || temporalCancelled || !image.completedSamples)throw std::runtime_error("Spatial denoise requires a completed, uncancelled image");
        if(!guides)guides=std::make_unique<PtGuides>(b);
        if(spatialEnabled && !spatial)spatial=std::make_unique<PtSpatial>(b);
        if(temporalEnabled && !temporal)temporal=std::make_unique<PtTemporal>(b);
        try {
            const auto guideStart=std::chrono::steady_clock::now();const bool guideDirty=guides->dirty;
            const auto& g=guides->run(sceneOwners,settings,temporalFrame,temporalFps,indexedGeometry,shaderDirectory,*acceleration,geometryBuffer,instanceBuffer,vertexBuffer,materialBuffer,false);
            stats.guideGpuMilliseconds=guideDirty?g.gpuMilliseconds:0;
            stats.guideCpuMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-guideStart).count();
            if(temporalEnabled)temporal->run(shaderDirectory,output,guides->output,previewBuffer,g,image.completedSamples,rawGeneration,exposure,toneMapping,stats,acceleration->timestampBits);
            if(spatialEnabled)spatial->run(shaderDirectory,output,guides->output,previewBuffer,settings.width,settings.height,image.completedSamples,exposure,toneMapping,stats,acceleration->timestampBits,temporalEnabled?&temporal->accumulated:nullptr);
            std::vector<unsigned char> candidate(previewRgba.size());void* mapped{};
            check(b.f.vkMapMemory(b.device,previewBuffer.memory,0,previewBuffer.size,0,&mapped),"Read spatial preview");
            const auto error=static_cast<const std::uint32_t*>(mapped)[static_cast<std::size_t>(settings.width)*settings.height];
            if(!error)std::memcpy(candidate.data(),mapped,candidate.size());
            b.f.vkUnmapMemory(b.device,previewBuffer.memory);
            if(error || b.errors.load())throw std::runtime_error("Invalid GPU spatial resolve; Raw accumulation retained");
            previewRgba=std::move(candidate);denoiseDirty=false;stats.previewReadbackBytes+=previewBuffer.size;
            stats.completedSampleGpuMilliseconds=stats.gpuMilliseconds+stats.accelerationGpuMilliseconds+stats.guideGpuMilliseconds
                +(temporalEnabled?stats.temporalGpuMilliseconds:0)+(spatialEnabled?stats.spatialGpuMilliseconds:0);
            stats.validationWarnings=b.warnings.load();stats.validationErrors=b.errors.load();refreshMemoryStatistics();
            gpuCapture.finish();
        } catch(...) {displayDirty=true;refreshMemoryStatistics();if(temporal)temporal->invalidate();if((spatial && spatial->inFlight) || (temporal && temporal->inFlight) || guides->inFlight || b.errors.load())usable=false;throw;}
    }

};
VulkanPathTracer::VulkanPathTracer(const std::filesystem::path& shaders,pathtracer::SceneSnapshot scene,pathtracer::RenderSettings settings,bool validation,bool manageGlfw):impl_(std::make_unique<Impl>(shaders,scene,settings,validation,manageGlfw)){}
VulkanPathTracer::~VulkanPathTracer()=default;
bool VulkanPathTracer::renderPass(const std::atomic<bool>* cancel,bool raw){return impl_->pass(cancel,raw);}
bool VulkanPathTracer::renderPreview(float exposure,bool toneMapping,bool wait){return impl_->preview(exposure,toneMapping,wait);}
unsigned VulkanPathTracer::completedSamples()const{return impl_->usable?impl_->image.completedSamples:0;}
const std::vector<unsigned char>& VulkanPathTracer::previewRgba()const{if(!impl_->usable)throw std::runtime_error("GPU PT state requires backend rebuild");return impl_->previewRgba;}
bool VulkanPathTracer::restart(const pathtracer::SceneSnapshot& scene,pathtracer::RenderSettings settings){return impl_->restart(scene,settings);}
const pathtracer::RenderImage& VulkanPathTracer::image()const{impl_->complete(false,true);impl_->readRaw();return impl_->image;}
const char* VulkanPathTracer::raygenShaderName()const{return impl_->raygenProgram();}
const RasterStatistics& VulkanPathTracer::statistics()const{return impl_->stats;}
void VulkanPathTracer::setTemporalDenoise(bool enabled){
    if(enabled!=impl_->temporalEnabled){impl_->temporalEnabled=enabled;if(impl_->temporal)impl_->temporal->invalidate();impl_->displayDirty=true;}
}
const pathtracer::RenderImage& VulkanPathTracer::accumulatedImage(){
    impl_->complete(false,true);
    if(!impl_->temporalEnabled || !impl_->usable || impl_->temporalCancelled || !impl_->image.completedSamples)throw std::runtime_error("Temporal denoise requires an enabled, completed, uncancelled image");
    if(!impl_->temporal || !impl_->temporal->valid || impl_->temporal->samples!=impl_->image.completedSamples || impl_->displayDirty || impl_->denoiseDirty){impl_->resolveDenoise();impl_->displayDirty=false;}
    return impl_->temporal->readImage(impl_->stats);
}
void VulkanPathTracer::setSpatialDenoise(bool enabled){if(enabled!=impl_->spatialEnabled){impl_->spatialEnabled=enabled;impl_->displayDirty=true;}}
const pathtracer::RenderImage& VulkanPathTracer::spatialImage(){
    impl_->complete(false,true);
    if(!impl_->spatialEnabled || !impl_->usable || impl_->temporalCancelled || !impl_->image.completedSamples)throw std::runtime_error("Spatial denoise requires an enabled, completed, uncancelled image");
    if(!impl_->spatial || impl_->spatial->samples!=impl_->image.completedSamples || impl_->displayDirty || impl_->denoiseDirty){impl_->resolveDenoise();impl_->displayDirty=false;}
    return impl_->spatial->readImage(impl_->stats);
}
void VulkanPathTracer::setTemporalFrame(int frame,int fps,bool cut){
    if(fps<=0)throw std::runtime_error("GPU PT temporal FPS must be positive");
    if(impl_->guides && (frame!=impl_->temporalFrame || fps!=impl_->temporalFps)){
        impl_->guides->dirty=true;
        if(fps!=impl_->temporalFps || (frame!=impl_->temporalFrame && static_cast<std::int64_t>(frame)!=static_cast<std::int64_t>(impl_->temporalFrame)+1))impl_->guides->invalidate();
    }
    if(impl_->temporalEnabled && (frame!=impl_->temporalFrame || fps!=impl_->temporalFps))impl_->displayDirty=true;
    impl_->temporalFrame=frame;impl_->temporalFps=fps;if(cut)invalidateTemporalHistory();
}
void VulkanPathTracer::invalidateTemporalHistory(){
    // Drain before invalidation so restart cannot recreate history across a cut.
    if(impl_->pending && !impl_->temporalCancelled)impl_->complete(false,true);
    if(impl_->guides)impl_->guides->invalidate();
    if(impl_->temporal)impl_->temporal->invalidate();
    impl_->displayDirty=true;
}
const TemporalGuideFrame& VulkanPathTracer::temporalGuides(){
    impl_->complete(false,true);
    if(!impl_->usable || impl_->temporalCancelled || !impl_->image.completedSamples)throw std::runtime_error("GPU PT guides require a completed, uncancelled image");
    if(!impl_->guides)impl_->guides=std::make_unique<detail::PtGuides>(impl_->b);
    auto& p=*impl_;
    try {
        const auto& result=p.guides->run(p.sceneOwners,p.settings,p.temporalFrame,p.temporalFps,p.indexedGeometry,p.shaderDirectory,*p.acceleration,p.geometryBuffer,p.instanceBuffer,p.vertexBuffer,p.materialBuffer);
        p.refreshMemoryStatistics();p.stats.validationWarnings=p.b.warnings.load();p.stats.validationErrors=p.b.errors.load();return result;
    } catch(...){p.guides->invalidate();p.refreshMemoryStatistics();if(p.guides->inFlight || p.b.errors.load())p.usable=false;throw;}
}
}
