// Previous history is frozen for a Raw generation/frame, including progressive resolves.
namespace detail {
struct PtTemporal {
    Bootstrap& b;
    std::unique_ptr<RasterResources> resources;
    RasterResources::Buffer previous,current,accumulated,status;
    VkDescriptorSet descriptors{};
    std::string programIdentity;
    unsigned width{},height{},samples{},previousSamples{};
    std::uint64_t generation{},previousGeneration{};
    int frame{},previousFrame{};
    bool valid{false},previousValid{false},inFlight{false};
    pathtracer::RenderImage image;
    explicit PtTemporal(Bootstrap& owner):b(owner){}
    void invalidate(){valid=previousValid=false;image={};}
    void run(const std::filesystem::path& shaders,const RasterResources::Buffer& raw,
        const RasterResources::Buffer& guide,const RasterResources::Buffer& preview,
        const TemporalGuideFrame& guides,unsigned spp,std::uint64_t rawGeneration,
        float exposure,bool tone,RasterStatistics& stats,unsigned timestampBits) {
        const auto start=std::chrono::steady_clock::now();
        const auto program=shaders/"pt-temporal.comp.spv";capture::recordInput(program);
        const auto identity=capture::snapshot(program).fingerprint;
        const unsigned w=guides.width,h=guides.height;
        if(identity!=programIdentity || width!=w || height!=h){
            invalidate();auto candidate=std::make_unique<RasterResources>(b);auto& r=*candidate;
            const auto bytes=static_cast<VkDeviceSize>(w)*h;
            const auto a=r.buffer(bytes*64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),c=r.buffer(bytes*64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            const auto result=r.buffer(bytes*16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),counter=r.buffer(32,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            std::array<VkDescriptorSetLayoutBinding,7> bindings{};
            for(unsigned i=0;i<7;++i){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
            auto layout=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);layout.bindingCount=7;layout.pBindings=bindings.data();
            check(b.f.vkCreateDescriptorSetLayout(b.device,&layout,nullptr,&r.setLayout),"Create temporal descriptors");
            VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,32};
            auto pipelineLayout=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);pipelineLayout.setLayoutCount=1;pipelineLayout.pSetLayouts=&r.setLayout;pipelineLayout.pushConstantRangeCount=1;pipelineLayout.pPushConstantRanges=&push;
            check(b.f.vkCreatePipelineLayout(b.device,&pipelineLayout,nullptr,&r.layout),"Create temporal layout");
            auto pipeline=info<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);pipeline.layout=r.layout;
            pipeline.stage=info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=r.shader(program);pipeline.stage.pName="main";
            check(b.f.vkCreateComputePipelines(b.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&r.pipeline),"Create temporal pipeline");
            const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,7};
            auto pool=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&size;
            check(b.f.vkCreateDescriptorPool(b.device,&pool,nullptr,&r.descriptors),"Create temporal pool");
            VkDescriptorSet next{};auto allocate=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);allocate.descriptorPool=r.descriptors;allocate.descriptorSetCount=1;allocate.pSetLayouts=&r.setLayout;
            check(b.f.vkAllocateDescriptorSets(b.device,&allocate,&next),"Allocate temporal set");
            auto query=info<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;
            check(b.f.vkCreateQueryPool(b.device,&query,nullptr,&r.queries),"Create temporal timer");
            resources=std::move(candidate);previous=a;current=c;accumulated=result;status=counter;descriptors=next;width=w;height=h;programIdentity=identity;
        }
        if(valid && (generation!=rawGeneration || frame!=guides.frame)){
            std::swap(previous,current);previousGeneration=generation;previousFrame=frame;previousSamples=samples;previousValid=true;
        }
        const bool sameRaw=previousGeneration==rawGeneration;
        // A larger cumulative prefix overlaps the old prefix. Use current Raw instead.
        const bool accept=previousValid && guides.historyValid && previousFrame==guides.previousFrame && (!sameRaw || previousSamples==spp);
        auto& f=b.frames[0];auto& r=*resources;
        check(b.f.vkWaitForFences(b.device,1,&f.fence,VK_TRUE,UINT64_MAX),"Wait temporal frame");
        const VkDescriptorBufferInfo buffers[]{{raw.handle,0,raw.size},{guide.handle,0,guide.size},{previous.handle,0,previous.size},{current.handle,0,current.size},{accumulated.handle,0,accumulated.size},{status.handle,0,status.size},{preview.handle,0,preview.size}};
        std::array<VkWriteDescriptorSet,7> writes{};
        for(unsigned i=0;i<7;++i){auto& write=writes[i];write=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);write.dstSet=descriptors;write.dstBinding=i;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffers[i];}
        b.f.vkUpdateDescriptorSets(b.device,7,writes.data(),0,nullptr);
        void* mapped{};check(b.f.vkMapMemory(b.device,status.memory,0,status.size,0,&mapped),"Reset temporal status");std::memset(mapped,0,32);b.f.vkUnmapMemory(b.device,status.memory);
        check(b.f.vkResetCommandBuffer(f.command,0),"Reset temporal command");auto begin=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        check(b.f.vkBeginCommandBuffer(f.command,&begin),"Begin temporal command");
        auto barrier=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_READ_BIT|VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        b.f.vkCmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        b.f.vkCmdResetQueryPool(f.command,r.queries,0,2);b.f.vkCmdWriteTimestamp(f.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,r.queries,0);
        if(b.f.vkCmdBeginDebugUtilsLabelEXT){auto label=info<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);label.pLabelName="Iris.GPUPathTracer.TemporalReprojection";b.f.vkCmdBeginDebugUtilsLabelEXT(f.command,&label);}
        b.f.vkCmdBindPipeline(f.command,VK_PIPELINE_BIND_POINT_COMPUTE,r.pipeline);
        b.f.vkCmdBindDescriptorSets(f.command,VK_PIPELINE_BIND_POINT_COMPUTE,r.layout,0,1,&descriptors,0,nullptr);
        struct Push {std::uint32_t width,height,valid,repeat;float exposure;std::uint32_t tone,spp,reserved;};
        const Push push{w,h,accept?1U:0U,sameRaw?1U:0U,exposure,tone?1U:0U,spp,0};
        b.f.vkCmdPushConstants(f.command,r.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);b.f.vkCmdDispatch(f.command,(w+7)/8,(h+7)/8,1);
        if(b.f.vkCmdEndDebugUtilsLabelEXT)b.f.vkCmdEndDebugUtilsLabelEXT(f.command);
        barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        b.f.vkCmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        b.f.vkCmdWriteTimestamp(f.command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,r.queries,1);check(b.f.vkEndCommandBuffer(f.command),"End temporal command");
        auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.commandBufferCount=1;submit.pCommandBuffers=&f.command;inFlight=true;
        check(b.f.vkResetFences(b.device,1,&f.fence),"Reset temporal fence");check(b.f.vkQueueSubmit(b.queue,1,&submit,f.fence),"Submit temporal");
        check(b.f.vkWaitForFences(b.device,1,&f.fence,VK_TRUE,UINT64_MAX),"Complete temporal");inFlight=false;
        std::uint32_t counts[8]{};check(b.f.vkMapMemory(b.device,status.memory,0,status.size,0,&mapped),"Read temporal status");std::memcpy(counts,mapped,32);b.f.vkUnmapMemory(b.device,status.memory);
        stats.temporalReadbackBytes+=32;
        if(counts[0] || b.errors.load()){invalidate();throw std::runtime_error("Invalid GPU temporal resolve; Raw retained");}
        std::uint64_t ticks[2]{};check(b.f.vkGetQueryPoolResults(b.device,r.queries,0,2,sizeof(ticks),ticks,sizeof(ticks[0]),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"Read temporal timestamps");
        VkPhysicalDeviceProperties properties{};b.f.vkGetPhysicalDeviceProperties(b.physical,&properties);
        const auto mask=timestampBits==64?UINT64_MAX:((1ULL<<timestampBits)-1);
        stats.temporalGpuMilliseconds=static_cast<double>((ticks[1]-ticks[0])&mask)*properties.limits.timestampPeriod/1e6;
        stats.temporalCpuMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        stats.temporalAllocatedBytes=r.allocatedBytes;stats.temporalAccepted=counts[1];stats.temporalRejected=counts[2];stats.temporalClamped=counts[3];stats.temporalRepeated=counts[4];++stats.temporalDispatches;
        generation=rawGeneration;frame=guides.frame;samples=spp;valid=true;image={};
    }
    const pathtracer::RenderImage& readImage(RasterStatistics& stats){
        if(!valid)throw std::runtime_error("Temporal history unavailable");
        if(!image.sum.empty())return image;
        std::vector<glm::vec4> pixels(static_cast<std::size_t>(width)*height);void* mapped{};
        check(b.f.vkMapMemory(b.device,accumulated.memory,0,accumulated.size,0,&mapped),"Read accumulated image");std::memcpy(pixels.data(),mapped,static_cast<std::size_t>(accumulated.size));b.f.vkUnmapMemory(b.device,accumulated.memory);
        pathtracer::RenderImage next;next.width=width;next.height=height;next.completedSamples=samples;next.sampleCounts.assign(pixels.size(),samples);next.sum.reserve(pixels.size());
        for(const auto& p:pixels){for(int c=0;c<4;++c)if(!std::isfinite(p[c]) || p[c]<0)throw std::runtime_error("Nonfinite accumulated image");next.sum.push_back(glm::vec3(p)*static_cast<float>(samples));}
        image=std::move(next);stats.temporalReadbackBytes+=accumulated.size;return image;
    }
};
}
