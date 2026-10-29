// Persistent, local-space BLAS and rigid-instance TLAS. Included inside detail.
struct RayQueryScene {
    Bootstrap& b;
    RasterResources storage;
    std::vector<std::vector<glm::vec3>> geometry;
    std::vector<VkAccelerationStructureKHR> blas;
    VkAccelerationStructureKHR tlas{};
    RasterResources::Buffer instances{},scratch{};
    std::vector<VkAccelerationStructureInstanceKHR> previousInstances;
    std::vector<std::uint32_t> meshMapping;
    std::optional<std::uint64_t> rasterSourceKey;
    std::uint32_t instanceCount{};
    VkDeviceSize scratchAlignment{256};
    std::uint32_t timestampBits{};
    std::uint64_t maxPrimitives{},maxInstances{};
    bool pathTracing{false};
    explicit RayQueryScene(Bootstrap& owner):b(owner),storage(owner) {
        auto acceleration=info<VkPhysicalDeviceAccelerationStructurePropertiesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR);
        auto properties=info<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);properties.pNext=&acceleration;
        b.f.vkGetPhysicalDeviceProperties2(b.physical,&properties);scratchAlignment=acceleration.minAccelerationStructureScratchOffsetAlignment;
        maxPrimitives=acceleration.maxPrimitiveCount;maxInstances=acceleration.maxInstanceCount;
        std::uint32_t count=0;b.f.vkGetPhysicalDeviceQueueFamilyProperties(b.physical,&count,nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);b.f.vkGetPhysicalDeviceQueueFamilyProperties(b.physical,&count,queues.data());
        timestampBits=queues.at(b.family).timestampValidBits;
        if(!timestampBits)throw std::runtime_error("Ray Query baseline requires graphics timestamps");
    }
    ~RayQueryScene() {
        b.f.vkDeviceWaitIdle(b.device);
        if(tlas)b.f.vkDestroyAccelerationStructureKHR(b.device,tlas,nullptr);
        for(auto handle:blas)b.f.vkDestroyAccelerationStructureKHR(b.device,handle,nullptr);
    }
    VkDeviceAddress address(const RasterResources::Buffer& buffer) {
        auto value=info<VkBufferDeviceAddressInfo>(VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);value.buffer=buffer.handle;
        return b.f.vkGetBufferDeviceAddress(b.device,&value);
    }
    VkDeviceAddress address(VkAccelerationStructureKHR handle) {
        auto value=info<VkAccelerationStructureDeviceAddressInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);value.accelerationStructure=handle;
        return b.f.vkGetAccelerationStructureDeviceAddressKHR(b.device,&value);
    }
    VkDeviceAddress alignedScratch(const RasterResources::Buffer& buffer) {
        const auto value=address(buffer);return (value+scratchAlignment-1)&~(scratchAlignment-1);
    }
    VkAccelerationStructureKHR create(VkAccelerationStructureTypeKHR type,VkDeviceSize bytes,const char* label) {
        const auto buffer=storage.buffer(bytes,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,nullptr,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        auto create=info<VkAccelerationStructureCreateInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
        create.buffer=buffer.handle;create.size=bytes;create.type=type;
        VkAccelerationStructureKHR result{};check(b.f.vkCreateAccelerationStructureKHR(b.device,&create,nullptr,&result),"Create acceleration structure");
        // Register ownership before optional naming, which can throw.
        if(type==VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR)tlas=result;else blas.push_back(result);
        b.label(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR,result,label);return result;
    }
    void barrier(VkCommandBuffer command,VkPipelineStageFlags dst=VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR) {
        auto memory=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        memory.srcAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_HOST_WRITE_BIT;
        memory.dstAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        if(dst==VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR)memory.dstAccessMask|=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        const auto stages=VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT
            |(b.rayTracingPipelineAvailable?VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR:0);
        b.f.vkCmdPipelineBarrier(command,stages,dst,0,1,&memory,0,nullptr,0,nullptr);
    }
    void buildBlas(VkCommandBuffer command) {
        // RQ builds are ordered by barrier() below, so their scratch can be reused.
        // Retain PT's allocation order: sharing regressed the small-scene trace
        // benchmark despite identical images. RQ also reuses this for TLAS updates.
        VkDeviceSize scratchBytes=0;
        if(!pathTracing)for(const auto& mesh:geometry) {
            if(mesh.size()>UINT32_MAX || mesh.size()/3>maxPrimitives)throw std::runtime_error("Ray Query mesh exceeds device geometry limits");
            auto shape=info<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
            shape.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;shape.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
            auto& triangles=shape.geometry.triangles;triangles.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            triangles.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;triangles.vertexStride=sizeof(glm::vec3);
            triangles.maxVertex=static_cast<std::uint32_t>(mesh.size()-1);triangles.indexType=VK_INDEX_TYPE_NONE_KHR;
            auto build=info<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
            build.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=1;build.pGeometries=&shape;
            const auto count=static_cast<std::uint32_t>(mesh.size()/3);
            auto sizes=info<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
            b.f.vkGetAccelerationStructureBuildSizesKHR(b.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&count,&sizes);
            scratchBytes=std::max(scratchBytes,sizes.buildScratchSize+scratchAlignment);
        }
        if(scratchBytes)scratch=storage.buffer(scratchBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,nullptr,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        for(const auto& mesh:geometry) {
            if(mesh.size()>UINT32_MAX || mesh.size()/3>maxPrimitives)throw std::runtime_error("Ray Query mesh exceeds device geometry limits");
            const auto vertices=storage.buffer(mesh.size()*sizeof(glm::vec3),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,mesh.data());
            auto shape=info<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
            shape.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;shape.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
            auto& triangles=shape.geometry.triangles;triangles.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            triangles.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;triangles.vertexData.deviceAddress=address(vertices);triangles.vertexStride=sizeof(glm::vec3);
            triangles.maxVertex=static_cast<std::uint32_t>(mesh.size()-1);triangles.indexType=VK_INDEX_TYPE_NONE_KHR;
            auto build=info<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
            build.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=1;build.pGeometries=&shape;
            const auto count=static_cast<std::uint32_t>(mesh.size()/3);
            auto sizes=info<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
            b.f.vkGetAccelerationStructureBuildSizesKHR(b.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&count,&sizes);
            build.dstAccelerationStructure=create(build.type,sizes.accelerationStructureSize,"Iris.RayQuery.BLAS");
            const auto temp=pathTracing?storage.buffer(sizes.buildScratchSize+scratchAlignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,nullptr,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT):scratch;
            build.scratchData.deviceAddress=alignedScratch(temp);
            const VkAccelerationStructureBuildRangeInfoKHR range{count,0,0,0};const auto* ranges=&range;
            b.f.vkCmdBuildAccelerationStructuresKHR(command,1,&build,&ranges);barrier(command);
        }
    }
    void buildTlas(VkCommandBuffer command,const std::vector<VkAccelerationStructureInstanceKHR>& data,bool update) {
        if(data.size()>maxInstances || data.size()>UINT32_MAX)throw std::runtime_error("Ray Query scene exceeds device instance limit");
        if(!update)instances=storage.buffer(data.size()*sizeof(data[0]),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,data.data());
        else if(!data.empty()) {
            void* mapped{};check(b.f.vkMapMemory(b.device,instances.memory,0,data.size()*sizeof(data[0]),0,&mapped),"Map TLAS instances");
            std::memcpy(mapped,data.data(),data.size()*sizeof(data[0]));b.f.vkUnmapMemory(b.device,instances.memory);
        }
        auto shape=info<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
        shape.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;shape.geometry.instances.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        shape.geometry.instances.data.deviceAddress=address(instances);
        auto build=info<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        build.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR|VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        build.mode=update?VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR:VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.geometryCount=1;build.pGeometries=&shape;const auto count=static_cast<std::uint32_t>(data.size());
        auto sizes=info<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        b.f.vkGetAccelerationStructureBuildSizesKHR(b.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&count,&sizes);
        if(!update) {
            create(build.type,sizes.accelerationStructureSize,"Iris.RayQuery.TLAS");
            const auto bytes=std::max(sizes.buildScratchSize,sizes.updateScratchSize)+scratchAlignment;
            if(pathTracing || scratch.size<bytes)scratch=storage.buffer(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,nullptr,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }
        build.dstAccelerationStructure=tlas;build.srcAccelerationStructure=update?tlas:VK_NULL_HANDLE;build.scratchData.deviceAddress=alignedScratch(scratch);
        const VkAccelerationStructureBuildRangeInfoKHR range{count,0,0,0};const auto* ranges=&range;
        barrier(command);b.f.vkCmdBuildAccelerationStructuresKHR(command,1,&build,&ranges);barrier(command,pathTracing?VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        instanceCount=count;
    }
    static void collect(const pathtracer::SceneSnapshot& scene,std::vector<std::vector<glm::vec3>>& meshes,std::vector<std::uint32_t>& mapping,bool all=false) {
        for(const auto& instance:scene.instances()) {
            if(!instance.castsShadow && !all)continue;
            const auto& model=*scene.assets().at(instance.assetIndex).model;const auto& mesh=model.meshes.at(instance.meshIndex);
            std::vector<glm::vec3> positions;
            for(const auto& part:mesh.submeshes)for(std::size_t i=part.firstIndex;i<static_cast<std::size_t>(part.firstIndex)+part.indexCount;++i)
                positions.push_back(mesh.vertices.at(mesh.indices.at(i)).position);
            if(positions.empty())continue;
            // ponytail: exact linear geometry comparison; replace only if CPU scene
            // preparation becomes material at the measured unique-mesh budget.
            auto found=std::find(meshes.begin(),meshes.end(),positions);
            if(found==meshes.end()){mapping.push_back(static_cast<std::uint32_t>(meshes.size()));meshes.push_back(std::move(positions));}
            else mapping.push_back(static_cast<std::uint32_t>(found-meshes.begin()));
        }
    }
    std::vector<VkAccelerationStructureInstanceKHR> instanceData(const pathtracer::SceneSnapshot& scene,const std::vector<std::uint32_t>& mapping) {
        std::vector<VkAccelerationStructureInstanceKHR> result;
        for(const auto& instance:scene.instances()) {
            if(!instance.castsShadow && !pathTracing)continue;
            const auto& mesh=scene.assets().at(instance.assetIndex).model->meshes.at(instance.meshIndex);
            if(std::none_of(mesh.submeshes.begin(),mesh.submeshes.end(),[](const auto& part){return part.indexCount!=0;}))continue;
            VkAccelerationStructureInstanceKHR item{};
            for(int row=0;row<3;++row)for(int col=0;col<4;++col)item.transform.matrix[row][col]=instance.objectToWorld[col][row];
            item.instanceCustomIndex=static_cast<unsigned>(result.size());
            item.mask=pathTracing?(instance.castsShadow?3:1):255;item.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            item.accelerationStructureReference=address(blas.at(mapping.at(result.size())));result.push_back(item);
        }
        return result;
    }
    static void prepare(Bootstrap& b,const pathtracer::SceneSnapshot& scene,std::unique_ptr<RayQueryScene>& current,RasterStatistics& stats,bool pathTracing=false,std::optional<std::uint64_t> sourceKey={}) {
        const auto start=std::chrono::steady_clock::now();
        // Raster has already checked every asset byte. Reuse only that content
        // identity plus caster membership; PT callers retain the original scan.
        const bool reuseCollection=current && !pathTracing && sourceKey && current->rasterSourceKey==sourceKey && current->pathTracing==pathTracing;
        std::vector<std::vector<glm::vec3>> meshes;std::vector<std::uint32_t> mapping;
        if(!reuseCollection)collect(scene,meshes,mapping,pathTracing);
        std::unique_ptr<RayQueryScene> candidate;
        const bool rebuild=!current || (!reuseCollection && (meshes!=current->geometry || mapping.size()!=current->instanceCount)) || current->pathTracing!=pathTracing;
        if(rebuild){candidate=std::make_unique<RayQueryScene>(b);candidate->geometry=std::move(meshes);candidate->pathTracing=pathTracing;}
        auto& active=rebuild?*candidate:*current;
        // Finish all BLAS before resolving their device addresses for TLAS instances.
        auto& frame=b.frames[0];
        auto execute=[&](const auto& record) {
            check(b.f.vkDeviceWaitIdle(b.device),"Wait before acceleration update");
            check(b.f.vkResetCommandBuffer(frame.command,0),"Reset acceleration commands");
            auto begin=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
            check(b.f.vkBeginCommandBuffer(frame.command,&begin),"Begin acceleration commands");
            RasterResources queryOwner(b);auto query=info<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;
            check(b.f.vkCreateQueryPool(b.device,&query,nullptr,&queryOwner.queries),"Create acceleration timer");
            b.f.vkCmdResetQueryPool(frame.command,queryOwner.queries,0,2);b.f.vkCmdWriteTimestamp(frame.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queryOwner.queries,0);
            if(b.f.vkCmdBeginDebugUtilsLabelEXT){auto label=info<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);label.pLabelName="Iris.RayQuery.BuildOrUpdate";b.f.vkCmdBeginDebugUtilsLabelEXT(frame.command,&label);}
            record(frame.command);
            if(b.f.vkCmdEndDebugUtilsLabelEXT)b.f.vkCmdEndDebugUtilsLabelEXT(frame.command);
            b.f.vkCmdWriteTimestamp(frame.command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queryOwner.queries,1);
            check(b.f.vkEndCommandBuffer(frame.command),"End acceleration commands");
            auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.commandBufferCount=1;submit.pCommandBuffers=&frame.command;
            check(b.f.vkQueueSubmit(b.queue,1,&submit,VK_NULL_HANDLE),"Submit acceleration build");check(b.f.vkDeviceWaitIdle(b.device),"Wait acceleration build");
            std::uint64_t ticks[2]{};check(b.f.vkGetQueryPoolResults(b.device,queryOwner.queries,0,2,sizeof(ticks),ticks,sizeof(ticks[0]),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"Read acceleration timer");
            VkPhysicalDeviceProperties properties{};b.f.vkGetPhysicalDeviceProperties(b.physical,&properties);
            const auto mask=active.timestampBits==64?UINT64_MAX:((1ULL<<active.timestampBits)-1);
            stats.accelerationGpuMilliseconds+=static_cast<double>((ticks[1]-ticks[0])&mask)*properties.limits.timestampPeriod/1e6;
        };
        if(rebuild && !active.geometry.empty()){execute([&](VkCommandBuffer command){active.buildBlas(command);});stats.blasBuilds=static_cast<unsigned>(active.blas.size());}
        const auto data=active.instanceData(scene,reuseCollection?active.meshMapping:mapping);
        if(rebuild || data.size()!=active.previousInstances.size() || (!data.empty() && std::memcmp(data.data(),active.previousInstances.data(),data.size()*sizeof(data[0]))!=0)) {
            execute([&](VkCommandBuffer command){active.buildTlas(command,data,!rebuild);});
            if(rebuild)stats.tlasBuilds=1;else stats.tlasUpdates=1;
            active.previousInstances=data;
        }
        if(!reuseCollection)active.meshMapping=std::move(mapping);
        active.rasterSourceKey=sourceKey;
        if(rebuild)current=std::move(candidate);
        stats.blasCount=static_cast<unsigned>(current->blas.size());stats.instanceCount=current->instanceCount;
        stats.accelerationBytes=current->storage.allocatedBytes;
        stats.accelerationCpuMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
};
