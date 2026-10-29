// Optional GPU-only A-Trous resolve. Raw accumulation and primary guides are read-only.
namespace detail {
struct PtSpatial {
    Bootstrap& b;
    std::unique_ptr<RasterResources> resources;
    RasterResources::Buffer ping,pong;
    VkDescriptorSet sets[2]{};
    std::string programIdentity;
    VkBuffer rawHandle{},guideHandle{},previewHandle{},accumulatedHandle{};
    unsigned width{},height{},samples{};
    bool inFlight{false};
    pathtracer::RenderImage image;
    explicit PtSpatial(Bootstrap& owner):b(owner){}
    void run(const std::filesystem::path& shaders,const RasterResources::Buffer& raw,
        const RasterResources::Buffer& guide,const RasterResources::Buffer& preview,
        unsigned w,unsigned h,unsigned spp,float exposure,bool tone,RasterStatistics& stats,unsigned timestampBits,const RasterResources::Buffer* accumulated=nullptr) {
        const auto start=std::chrono::steady_clock::now();
        const auto program=shaders/(accumulated?"pt-spatial.temporal.comp.spv":"pt-spatial.comp.spv");capture::recordInput(program);
        const auto identity=capture::snapshot(program).fingerprint;
        if(accumulatedHandle!=(accumulated?accumulated->handle:VK_NULL_HANDLE) || identity!=programIdentity || rawHandle!=raw.handle || guideHandle!=guide.handle || previewHandle!=preview.handle || width!=w || height!=h){
            auto candidate=std::make_unique<RasterResources>(b);auto& r=*candidate;
            const auto bytes=static_cast<VkDeviceSize>(w)*h*sizeof(glm::vec4);
            const auto a=r.buffer(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),c=r.buffer(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            std::array<VkDescriptorSetLayoutBinding,5> bindings{};
            for(unsigned i=0;i<5;++i){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
            auto layout=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);layout.bindingCount=5;layout.pBindings=bindings.data();
            check(b.f.vkCreateDescriptorSetLayout(b.device,&layout,nullptr,&r.setLayout),"Create spatial descriptors");
            VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,20};
            auto pipelineLayout=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);pipelineLayout.setLayoutCount=1;pipelineLayout.pSetLayouts=&r.setLayout;pipelineLayout.pushConstantRangeCount=1;pipelineLayout.pPushConstantRanges=&push;
            check(b.f.vkCreatePipelineLayout(b.device,&pipelineLayout,nullptr,&r.layout),"Create spatial layout");
            auto pipeline=info<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);pipeline.layout=r.layout;
            pipeline.stage=info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=r.shader(program);pipeline.stage.pName="main";
            check(b.f.vkCreateComputePipelines(b.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&r.pipeline),"Create spatial pipeline");
            const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,10};
            auto pool=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool.maxSets=2;pool.poolSizeCount=1;pool.pPoolSizes=&size;
            check(b.f.vkCreateDescriptorPool(b.device,&pool,nullptr,&r.descriptors),"Create spatial pool");
            VkDescriptorSet next[2]{};const VkDescriptorSetLayout layouts[]{r.setLayout,r.setLayout};
            auto allocate=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);allocate.descriptorPool=r.descriptors;allocate.descriptorSetCount=2;allocate.pSetLayouts=layouts;
            check(b.f.vkAllocateDescriptorSets(b.device,&allocate,next),"Allocate spatial sets");
            for(unsigned pass=0;pass<2;++pass){
                const auto& source=pass==0?(accumulated?*accumulated:c):a;const auto& target=pass==0?a:c;
                const VkDescriptorBufferInfo buffers[]{{raw.handle,0,raw.size},{guide.handle,0,guide.size},{source.handle,0,source.size},{target.handle,0,target.size},{preview.handle,0,preview.size}};
                std::array<VkWriteDescriptorSet,5> writes{};
                for(unsigned i=0;i<5;++i){auto& write=writes[i];write=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);write.dstSet=next[pass];write.dstBinding=i;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffers[i];}
                b.f.vkUpdateDescriptorSets(b.device,5,writes.data(),0,nullptr);
            }
            auto query=info<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;
            check(b.f.vkCreateQueryPool(b.device,&query,nullptr,&r.queries),"Create spatial timer");
            resources=std::move(candidate);accumulatedHandle=accumulated?accumulated->handle:VK_NULL_HANDLE;ping=a;pong=c;sets[0]=next[0];sets[1]=next[1];
            rawHandle=raw.handle;guideHandle=guide.handle;previewHandle=preview.handle;width=w;height=h;programIdentity=identity;
        }
        auto& f=b.frames[0];auto& r=*resources;
        check(b.f.vkWaitForFences(b.device,1,&f.fence,VK_TRUE,UINT64_MAX),"Wait spatial frame");
        void* mapped{};check(b.f.vkMapMemory(b.device,preview.memory,0,preview.size,0,&mapped),"Reset spatial status");
        static_cast<std::uint32_t*>(mapped)[static_cast<std::size_t>(w)*h]=0;b.f.vkUnmapMemory(b.device,preview.memory);
        check(b.f.vkResetCommandBuffer(f.command,0),"Reset spatial command");auto begin=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        check(b.f.vkBeginCommandBuffer(f.command,&begin),"Begin spatial command");
        auto barrier=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_READ_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        b.f.vkCmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        b.f.vkCmdResetQueryPool(f.command,r.queries,0,2);b.f.vkCmdWriteTimestamp(f.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,r.queries,0);
        if(b.f.vkCmdBeginDebugUtilsLabelEXT){auto label=info<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);label.pLabelName="Iris.GPUPathTracer.SpatialATrous";b.f.vkCmdBeginDebugUtilsLabelEXT(f.command,&label);}
        b.f.vkCmdBindPipeline(f.command,VK_PIPELINE_BIND_POINT_COMPUTE,r.pipeline);
        for(unsigned pass=0;pass<2;++pass){
            b.f.vkCmdBindDescriptorSets(f.command,VK_PIPELINE_BIND_POINT_COMPUTE,r.layout,0,1,&sets[pass],0,nullptr);
            struct Push {std::uint32_t width,height,iteration,tone;float exposure;};const Push push{w,h,pass,tone?1U:0U,exposure};
            b.f.vkCmdPushConstants(f.command,r.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);b.f.vkCmdDispatch(f.command,(w+7)/8,(h+7)/8,1);
            barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            b.f.vkCmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        }
        if(b.f.vkCmdEndDebugUtilsLabelEXT)b.f.vkCmdEndDebugUtilsLabelEXT(f.command);
        barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        b.f.vkCmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        b.f.vkCmdWriteTimestamp(f.command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,r.queries,1);check(b.f.vkEndCommandBuffer(f.command),"End spatial command");
        auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.commandBufferCount=1;submit.pCommandBuffers=&f.command;inFlight=true;
        check(b.f.vkResetFences(b.device,1,&f.fence),"Reset spatial fence");check(b.f.vkQueueSubmit(b.queue,1,&submit,f.fence),"Submit spatial");
        check(b.f.vkWaitForFences(b.device,1,&f.fence,VK_TRUE,UINT64_MAX),"Complete spatial");inFlight=false;
        std::uint64_t ticks[2]{};check(b.f.vkGetQueryPoolResults(b.device,r.queries,0,2,sizeof(ticks),ticks,sizeof(ticks[0]),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"Read spatial timestamps");
        VkPhysicalDeviceProperties properties{};b.f.vkGetPhysicalDeviceProperties(b.physical,&properties);
        const auto mask=timestampBits==64?UINT64_MAX:((1ULL<<timestampBits)-1);
        stats.spatialGpuMilliseconds=static_cast<double>((ticks[1]-ticks[0])&mask)*properties.limits.timestampPeriod/1e6;
        stats.spatialCpuMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        stats.spatialAllocatedBytes=r.allocatedBytes;samples=spp;image={};++stats.spatialDispatches;
    }
    const pathtracer::RenderImage& readImage(RasterStatistics& stats) {
        if(!image.sum.empty())return image;
        std::vector<glm::vec4> pixels(static_cast<std::size_t>(width)*height);void* mapped{};
        check(b.f.vkMapMemory(b.device,pong.memory,0,pong.size,0,&mapped),"Read spatial image");
        std::memcpy(pixels.data(),mapped,static_cast<std::size_t>(pong.size));b.f.vkUnmapMemory(b.device,pong.memory);
        pathtracer::RenderImage next;next.width=width;next.height=height;next.completedSamples=samples;next.sampleCounts.assign(pixels.size(),samples);next.sum.reserve(pixels.size());
        for(const auto& p:pixels){for(int c=0;c<3;++c)if(!std::isfinite(p[c]) || p[c]<0)throw std::runtime_error("Nonfinite spatial image");next.sum.push_back(glm::vec3(p)*static_cast<float>(samples));}
        image=std::move(next);stats.spatialReadbackBytes+=pong.size;return image;
    }
};
}
