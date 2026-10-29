// Private, optional primary-surface pass. The integrator's descriptors, buffers,
// random sequence and timestamps are never modified by this pass.
namespace detail {
struct PtGuidePose {glm::mat4 currentToPrevious{1},normalToPrevious{1};glm::uvec4 identity{};};
struct PtGuideUniform {
    glm::mat4 inverseView{1},currentView{1},previousView{1};
    glm::vec4 eyeScale{},previousScale{};glm::uvec4 extent{};
};
static_assert(sizeof(PtGuidePose)==144 && sizeof(PtGuideUniform)==240 && sizeof(TemporalGuidePixel)==64,"Guide GPU layout");
struct PtGuides {
    Bootstrap& b;
    std::unique_ptr<RasterResources> resources;
    RasterResources::Buffer output,poses,uniform;
    VkDescriptorSet descriptors{};
    TemporalGuideFrame result;
    // Only the previous published camera and poses, never another Scene snapshot.
    std::vector<glm::mat4> previousTransforms;
    float previousFov{};
    bool previousValid{false},dirty{true},cancelled{false},inFlight{false};
    std::string programIdentity;
    std::uint64_t readbackBytes{0};
    explicit PtGuides(Bootstrap& owner):b(owner){}
    void invalidate(){previousValid=false;dirty=true;}
    void readPixels() {
        if(!result.pixels.empty())return;
        const auto start=std::chrono::steady_clock::now();
        result.pixels.resize(static_cast<std::size_t>(result.width)*result.height);void* mapped{};
        check(b.f.vkMapMemory(b.device,output.memory,0,output.size,0,&mapped),"Read guides");
        std::memcpy(result.pixels.data(),mapped,static_cast<std::size_t>(output.size));b.f.vkUnmapMemory(b.device,output.memory);
        readbackBytes+=output.size;
        for(const auto& p:result.pixels)for(int c=0;c<4;++c)if(!std::isfinite(p.motionDepth[c]) || !std::isfinite(p.normal[c]) || !std::isfinite(p.previousNormal[c])){result.pixels.clear();throw std::runtime_error("Nonfinite GPU PT guides");}
        result.cpuMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    const TemporalGuideFrame& run(const pathtracer::SceneSnapshot& scene,const pathtracer::RenderSettings& settings,
        int frame,int fps,bool indexed,const std::filesystem::path& shaders,RayQueryScene& acceleration,
        const RasterResources::Buffer& geometry,const RasterResources::Buffer& instances,
        const RasterResources::Buffer& vertices,const RasterResources::Buffer& materials,bool readback=true) {
        if(cancelled)throw std::runtime_error("Cancelled GPU PT guides require restart");
        const auto started=std::chrono::steady_clock::now();
        const auto program=shaders/(indexed?"pt-guides.indexed.comp.spv":"pt-guides.comp.spv");
        capture::recordInput(program);
        const auto identity=capture::snapshot(program).fingerprint;
        if(identity!=programIdentity){invalidate();resources.reset();programIdentity=identity;}
        if(!dirty){if(readback)readPixels();return result;}
        const auto& camera=scene.camera();
        std::vector<glm::mat4> transforms;std::vector<glm::uvec4> identities;
        for(const auto& instance:scene.instances()){
            const auto& mesh=scene.assets()[instance.assetIndex].model->meshes[instance.meshIndex];
            bool nonempty=false;for(const auto& submesh:mesh.submeshes)nonempty|=submesh.indexCount!=0;
            if(!nonempty)continue;
            transforms.push_back(instance.objectToWorld);
            identities.emplace_back(static_cast<unsigned>(instance.entityId),static_cast<unsigned>(instance.entityId>>32),instance.meshIndex,0);
        }
        bool valid=previousValid && transforms.size()==previousTransforms.size() && result.width==settings.width && result.height==settings.height;
        valid=valid && fps==result.framesPerSecond && (frame==result.frame || (static_cast<std::int64_t>(frame)==static_cast<std::int64_t>(result.frame)+1));
        // A lens change or >45-degree orientation change is an automatic cut.
        const auto inverse=glm::inverse(camera.view);
        valid=valid && std::abs(camera.verticalFieldOfViewRadians-previousFov)<1e-6f;
        if(valid)valid=glm::dot(glm::normalize(glm::vec3(inverse[2])),glm::normalize(glm::vec3(glm::inverse(result.currentView)[2])))>.70710678f;
        std::vector<PtGuidePose> poseData(std::max<std::size_t>(1,transforms.size()));
        for(std::size_t i=0;i<transforms.size();++i){auto& p=poseData[i];p.identity=identities[i];
            if(valid){p.currentToPrevious=previousTransforms[i]*glm::inverse(transforms[i]);p.normalToPrevious=glm::transpose(glm::inverse(p.currentToPrevious));}}
        PtGuideUniform data;data.inverseView=inverse;data.currentView=camera.view;data.previousView=valid?result.currentView:camera.view;
        data.eyeScale={camera.position,std::tan(camera.verticalFieldOfViewRadians*.5f)};
        data.previousScale={valid?std::tan(previousFov*.5f):data.eyeScale.w,0,0,0};data.extent={settings.width,settings.height,valid?1U:0U,0};
        const auto bytes=static_cast<std::size_t>(settings.width)*settings.height*sizeof(TemporalGuidePixel);
        if(!resources || output.size!=bytes || poses.size!=poseData.size()*sizeof(PtGuidePose)){
            std::uint32_t count=0;b.f.vkGetPhysicalDeviceQueueFamilyProperties(b.physical,&count,nullptr);
            std::vector<VkQueueFamilyProperties> queues(count);b.f.vkGetPhysicalDeviceQueueFamilyProperties(b.physical,&count,queues.data());
            VkPhysicalDeviceProperties properties{};b.f.vkGetPhysicalDeviceProperties(b.physical,&properties);
            if(!(queues[b.family].queueFlags&VK_QUEUE_COMPUTE_BIT) || !b.rayQueryAvailable || properties.limits.maxPerStageDescriptorStorageBuffers<6 || bytes>properties.limits.maxStorageBufferRange || poseData.size()*sizeof(PtGuidePose)>properties.limits.maxStorageBufferRange)
                throw std::runtime_error("GPU PT guide compute/storage capability unavailable");
            auto candidate=std::make_unique<RasterResources>(b);auto& r=*candidate;
            auto nextOutput=r.buffer(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            auto nextPoses=r.buffer(poseData.size()*sizeof(PtGuidePose),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            auto nextUniform=r.buffer(sizeof(data),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            std::array<VkDescriptorSetLayoutBinding,8> bindings{};
            for(unsigned i=0;i<8;++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
                bindings[i].descriptorType=i==0?VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:i==6?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;}
            auto layout=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);layout.bindingCount=8;layout.pBindings=bindings.data();
            check(b.f.vkCreateDescriptorSetLayout(b.device,&layout,nullptr,&r.setLayout),"Create guides set layout");
            auto pipelineLayout=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);pipelineLayout.setLayoutCount=1;pipelineLayout.pSetLayouts=&r.setLayout;
            check(b.f.vkCreatePipelineLayout(b.device,&pipelineLayout,nullptr,&r.layout),"Create guides pipeline layout");
            auto pipeline=info<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);pipeline.layout=r.layout;
            pipeline.stage=info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
            pipeline.stage.module=r.shader(shaders/(indexed?"pt-guides.indexed.comp.spv":"pt-guides.comp.spv"));pipeline.stage.pName="main";
            check(b.f.vkCreateComputePipelines(b.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&r.pipeline),"Create guides compute pipeline");
            const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,6},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};
            auto pool=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool.maxSets=1;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
            check(b.f.vkCreateDescriptorPool(b.device,&pool,nullptr,&r.descriptors),"Create guides pool");
            auto allocate=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);allocate.descriptorPool=r.descriptors;allocate.descriptorSetCount=1;allocate.pSetLayouts=&r.setLayout;
            VkDescriptorSet nextSet{};check(b.f.vkAllocateDescriptorSets(b.device,&allocate,&nextSet),"Allocate guides set");
            const auto& v=indexed?vertices:geometry;const auto& m=indexed?materials:geometry;
            VkDescriptorBufferInfo buffers[]{{geometry.handle,0,geometry.size},{instances.handle,0,instances.size},{v.handle,0,v.size},{m.handle,0,m.size},{nextPoses.handle,0,nextPoses.size},{nextUniform.handle,0,nextUniform.size},{nextOutput.handle,0,nextOutput.size}};
            auto as=info<VkWriteDescriptorSetAccelerationStructureKHR>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);as.accelerationStructureCount=1;as.pAccelerationStructures=&acceleration.tlas;
            std::array<VkWriteDescriptorSet,8> writes{};
            for(unsigned i=0;i<8;++i){auto& w=writes[i];w=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);w.dstSet=nextSet;w.dstBinding=i;w.descriptorCount=1;w.descriptorType=bindings[i].descriptorType;if(i==0)w.pNext=&as;else w.pBufferInfo=&buffers[i-1];}
            b.f.vkUpdateDescriptorSets(b.device,8,writes.data(),0,nullptr);
            auto query=info<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;
            check(b.f.vkCreateQueryPool(b.device,&query,nullptr,&r.queries),"Create guides timer");
            resources=std::move(candidate);output=nextOutput;poses=nextPoses;uniform=nextUniform;descriptors=nextSet;
        }
        const auto upload=[&](const RasterResources::Buffer& buffer,const void* source){void* mapped{};check(b.f.vkMapMemory(b.device,buffer.memory,0,buffer.size,0,&mapped),"Map guides upload");std::memcpy(mapped,source,static_cast<std::size_t>(buffer.size));b.f.vkUnmapMemory(b.device,buffer.memory);};
        auto& f=b.frames[0];auto& r=*resources;
        check(b.f.vkWaitForFences(b.device,1,&f.fence,VK_TRUE,UINT64_MAX),"Wait guides frame");
        upload(poses,poseData.data());upload(uniform,&data);
        check(b.f.vkResetCommandBuffer(f.command,0),"Reset guides command");auto begin=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        check(b.f.vkBeginCommandBuffer(f.command,&begin),"Begin guides command");
        auto barrier=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_READ_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        b.f.vkCmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        b.f.vkCmdResetQueryPool(f.command,r.queries,0,2);b.f.vkCmdWriteTimestamp(f.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,r.queries,0);
        b.f.vkCmdBindPipeline(f.command,VK_PIPELINE_BIND_POINT_COMPUTE,r.pipeline);b.f.vkCmdBindDescriptorSets(f.command,VK_PIPELINE_BIND_POINT_COMPUTE,r.layout,0,1,&descriptors,0,nullptr);
        b.f.vkCmdDispatch(f.command,(settings.width+7)/8,(settings.height+7)/8,1);
        barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        b.f.vkCmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        b.f.vkCmdWriteTimestamp(f.command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,r.queries,1);check(b.f.vkEndCommandBuffer(f.command),"End guides command");
        auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.commandBufferCount=1;submit.pCommandBuffers=&f.command;
        inFlight=true;
        check(b.f.vkResetFences(b.device,1,&f.fence),"Reset guides fence");check(b.f.vkQueueSubmit(b.queue,1,&submit,f.fence),"Submit guides");
        check(b.f.vkWaitForFences(b.device,1,&f.fence,VK_TRUE,UINT64_MAX),"Complete guides");
        inFlight=false;
        TemporalGuideFrame next;next.width=settings.width;next.height=settings.height;next.frame=frame;next.previousFrame=valid?result.frame:frame;next.framesPerSecond=fps;
        next.historyValid=valid;next.currentView=camera.view;next.previousView=data.previousView;next.allocatedBytes=r.allocatedBytes;
        next.currentVerticalFov=camera.verticalFieldOfViewRadians;next.previousVerticalFov=valid?previousFov:camera.verticalFieldOfViewRadians;
        std::uint64_t ticks[2]{};check(b.f.vkGetQueryPoolResults(b.device,r.queries,0,2,sizeof(ticks),ticks,sizeof(ticks[0]),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"Read guides timestamps");
        VkPhysicalDeviceProperties properties{};b.f.vkGetPhysicalDeviceProperties(b.physical,&properties);
        const auto mask=acceleration.timestampBits==64?UINT64_MAX:((1ULL<<acceleration.timestampBits)-1);
        next.gpuMilliseconds=static_cast<double>((ticks[1]-ticks[0])&mask)*properties.limits.timestampPeriod/1e6;
        if(b.errors.load())throw std::runtime_error("GPU PT guide validation errors");
        next.cpuMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        result=std::move(next);previousTransforms=std::move(transforms);previousFov=camera.verticalFieldOfViewRadians;previousValid=true;dirty=false;
        if(readback)readPixels();
        return result;
    }
};
}
