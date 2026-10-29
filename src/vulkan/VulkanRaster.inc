// Private implementation included by VulkanBootstrap.cpp to share the Vulkan
// dispatch and device owner. No Vulkan types escape the public CPU interface.
namespace iris::vulkan {
namespace detail {
struct RasterVertex { glm::vec3 position, normal; glm::vec2 uv; };
struct FrameUniform {
    glm::mat4 vp, inverseVP;
    glm::vec4 eyeExposure, sunDirectionTone, sunRadiance, environmentInfo, background;
    glm::vec4 localPosition[16]{}, localDirection[16]{}, localRadiance[16]{};
};
struct MaterialPush { glm::vec4 base, emissionMetallic, parameters; };
struct RasterResources {
    Bootstrap& b;
    struct Buffer { VkBuffer handle{}; VkDeviceMemory memory{}; VkDeviceSize size{}; std::uint32_t memoryType{}, memoryFlags{}; };
    struct Image { VkImage handle{}; VkDeviceMemory memory{}; VkImageView view{}; };
    std::vector<Buffer> buffers;
    std::vector<Image> textures;
    std::vector<VkShaderModule> shaders;
    VkSampler sampler{};
    VkDescriptorSetLayout setLayout{}; VkDescriptorPool descriptors{};
    VkPipelineLayout layout{}; VkPipeline pipeline{}, skyPipeline{};
    VkQueryPool queries{};
    std::uint64_t allocatedBytes{0};
    std::uint64_t deviceLocalBytes{0};
    std::uint64_t hostUploadBytes{0};
    std::uint32_t textureUploads{0}, pipelineCreations{0};
    explicit RasterResources(Bootstrap& owner):b(owner) {}
    ~RasterResources() {
        // Also covers an exception after a partial submit.
        if(b.device) b.f.vkDeviceWaitIdle(b.device);
        if(pipeline)b.f.vkDestroyPipeline(b.device,pipeline,nullptr);
        if(skyPipeline)b.f.vkDestroyPipeline(b.device,skyPipeline,nullptr);
        if(layout)b.f.vkDestroyPipelineLayout(b.device,layout,nullptr);
        if(descriptors)b.f.vkDestroyDescriptorPool(b.device,descriptors,nullptr);
        if(setLayout)b.f.vkDestroyDescriptorSetLayout(b.device,setLayout,nullptr);
        if(sampler)b.f.vkDestroySampler(b.device,sampler,nullptr);
        if(queries)b.f.vkDestroyQueryPool(b.device,queries,nullptr);
        for(auto shader:shaders)b.f.vkDestroyShaderModule(b.device,shader,nullptr);
        for(auto image:textures) {
            if(image.view)b.f.vkDestroyImageView(b.device,image.view,nullptr);
            if(image.handle)b.f.vkDestroyImage(b.device,image.handle,nullptr);
            if(image.memory)b.f.vkFreeMemory(b.device,image.memory,nullptr);
        }
        for(auto buffer:buffers) {
            if(buffer.handle)b.f.vkDestroyBuffer(b.device,buffer.handle,nullptr);
            if(buffer.memory)b.f.vkFreeMemory(b.device,buffer.memory,nullptr);
        }
    }
    VkDeviceMemory allocate(const VkMemoryRequirements& requirements,VkMemoryPropertyFlags flags,bool address=false,
                            std::uint32_t* selectedType=nullptr,std::uint32_t* selectedFlags=nullptr) {
        VkPhysicalDeviceMemoryProperties properties{};b.f.vkGetPhysicalDeviceMemoryProperties(b.physical,&properties);
        for(std::uint32_t i=0;i<properties.memoryTypeCount;++i) {
            if(!(requirements.memoryTypeBits&(1U<<i)) || (properties.memoryTypes[i].propertyFlags&flags)!=flags)continue;
            auto allocation=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
            auto allocationFlags=info<VkMemoryAllocateFlagsInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO);
            allocationFlags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            if(address)allocation.pNext=&allocationFlags;
            allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=i;
            VkDeviceMemory memory{};check(b.f.vkAllocateMemory(b.device,&allocation,nullptr,&memory),"Allocate raster memory");
            if(selectedType)*selectedType=i;
            if(selectedFlags)*selectedFlags=properties.memoryTypes[i].propertyFlags;
            allocatedBytes+=requirements.size;
            if(properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)deviceLocalBytes+=requirements.size;
            return memory;
        }
        throw std::runtime_error("Vulkan raster memory type unavailable");
    }
    Buffer buffer(VkDeviceSize size,VkBufferUsageFlags usage,const void* data=nullptr,
                  VkMemoryPropertyFlags flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
        buffers.push_back({});auto& result=buffers.back();result.size=std::max<VkDeviceSize>(size,16);
        auto create=info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        create.size=result.size;create.usage=usage;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(b.f.vkCreateBuffer(b.device,&create,nullptr,&result.handle),"Create raster buffer");
        VkMemoryRequirements requirements{};b.f.vkGetBufferMemoryRequirements(b.device,result.handle,&requirements);
        result.memory=allocate(requirements,flags,(usage&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)!=0,&result.memoryType,&result.memoryFlags);
        check(b.f.vkBindBufferMemory(b.device,result.handle,result.memory,0),"Bind raster buffer");
        if(data && size) {void* mapped{};check(b.f.vkMapMemory(b.device,result.memory,0,size,0,&mapped),"Map raster buffer");
            std::memcpy(mapped,data,static_cast<std::size_t>(size));b.f.vkUnmapMemory(b.device,result.memory);hostUploadBytes+=size;}
        b.label(VK_OBJECT_TYPE_BUFFER,result.handle,"Iris.Raster.Buffer."+std::to_string(buffers.size()));
        return result;
    }
    Image image(std::uint32_t width,std::uint32_t height,VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect,std::uint32_t levels=1) {
        textures.push_back({});auto& result=textures.back();
        auto create=info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        create.imageType=VK_IMAGE_TYPE_2D;create.format=format;create.extent={width,height,1};
        create.mipLevels=levels;create.arrayLayers=1;create.samples=VK_SAMPLE_COUNT_1_BIT;
        create.tiling=VK_IMAGE_TILING_OPTIMAL;create.usage=usage;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(b.f.vkCreateImage(b.device,&create,nullptr,&result.handle),"Create raster image");
        VkMemoryRequirements requirements{};b.f.vkGetImageMemoryRequirements(b.device,result.handle,&requirements);
        result.memory=allocate(requirements,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(b.f.vkBindImageMemory(b.device,result.handle,result.memory,0),"Bind raster image");
        auto view=info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view.image=result.handle;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;
        view.subresourceRange={aspect,0,levels,0,1};check(b.f.vkCreateImageView(b.device,&view,nullptr,&result.view),"Create raster view");
        b.label(VK_OBJECT_TYPE_IMAGE,result.handle,"Iris.Raster.Image."+std::to_string(textures.size()));return result;
    }
    void transition(VkCommandBuffer command,Image image,VkImageLayout oldLayout,VkImageLayout newLayout,
                    VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags source,VkAccessFlags destination,
                    VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT,std::uint32_t levels=1) {
        auto barrier=info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.oldLayout=oldLayout;barrier.newLayout=newLayout;barrier.srcAccessMask=source;barrier.dstAccessMask=destination;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image.handle;
        barrier.subresourceRange={aspect,0,levels,0,1};b.f.vkCmdPipelineBarrier(command,src,dst,0,0,nullptr,0,nullptr,1,&barrier);
    }
    Image texture(VkCommandBuffer command,std::uint32_t width,std::uint32_t height,const std::vector<glm::vec4>& pixels,VkPipelineStageFlags consumer=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,bool mipmaps=false,RasterResources* uploads=nullptr) {
        if(!width || !height || pixels.size()!=static_cast<std::size_t>(width)*height)throw std::runtime_error("Invalid Vulkan texture extent");
        // Filter decoded linear texels, including alpha and tangent-space normals.
        // Keep PT's level-zero sampling contract unless the raster caller opts in.
        std::vector<glm::vec4> texels;
        std::vector<VkBufferImageCopy> copies;
        std::uint32_t w=width,h=height;
        std::size_t offset=0;
        do {
            VkBufferImageCopy copy{};copy.bufferOffset=offset*sizeof(glm::vec4);
            copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,static_cast<std::uint32_t>(copies.size()),0,1};copy.imageExtent={w,h,1};copies.push_back(copy);
            if(!mipmaps || (w==1 && h==1))break;
            if(texels.empty())texels=pixels;
            const auto nextW=std::max(1U,w/2),nextH=std::max(1U,h/2);
            const auto nextOffset=texels.size();texels.resize(nextOffset+static_cast<std::size_t>(nextW)*nextH);
            for(std::uint32_t y=0;y<nextH;++y)for(std::uint32_t x=0;x<nextW;++x) {
                glm::vec4 sum(0);std::uint32_t count=0;
                // Include the last row/column of odd and one-dimensional images.
                for(auto sy=y*h/nextH;sy<(y+1)*h/nextH;++sy)for(auto sx=x*w/nextW;sx<(x+1)*w/nextW;++sx) {
                    sum+=texels[offset+static_cast<std::size_t>(sy)*w+sx];++count;
                }
                texels[nextOffset+static_cast<std::size_t>(y)*nextW+x]=sum/static_cast<float>(count);
            }
            offset=nextOffset;w=nextW;h=nextH;
        } while(true);
        const auto& upload=texels.empty()?pixels:texels;
        const auto levels=static_cast<std::uint32_t>(copies.size());
        const auto staging=(uploads?*uploads:*this).buffer(upload.size()*sizeof(glm::vec4),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,upload.data());
        const auto result=image(width,height,VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT,levels);
        ++textureUploads;
        transition(command,result,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_IMAGE_ASPECT_COLOR_BIT,levels);
        b.f.vkCmdCopyBufferToImage(command,staging.handle,result.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,levels,copies.data());
        transition(command,result,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,consumer,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_ASPECT_COLOR_BIT,levels);
        return result;
    }
    VkShaderModule shader(const std::filesystem::path& path) {
        capture::recordInput(path);
        std::ifstream input(path,std::ios::binary|std::ios::ate);
        if(!input)throw std::runtime_error("Cannot open Vulkan shader: "+path.string());
        const auto bytes=input.tellg();if(bytes<=0 || bytes%4!=0)throw std::runtime_error("Invalid SPIR-V size: "+path.string());
        std::vector<std::uint32_t> code(static_cast<std::size_t>(bytes)/4);input.seekg(0);input.read(reinterpret_cast<char*>(code.data()),bytes);
        if(!input || code[0]!=0x07230203)throw std::runtime_error("Invalid SPIR-V: "+path.string());
        auto create=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);create.codeSize=static_cast<std::size_t>(bytes);create.pCode=code.data();
        VkShaderModule result{};check(b.f.vkCreateShaderModule(b.device,&create,nullptr,&result),"Create raster shader");shaders.push_back(result);return result;
    }
    void createPipeline(const std::filesystem::path& directory,bool rayQuery=false) {
        std::vector<VkDescriptorSetLayoutBinding> bindings(rayQuery?6:5);
        for(std::uint32_t i=0;i<bindings.size();++i) {bindings[i].binding=i;bindings[i].descriptorCount=1;
            bindings[i].descriptorType=i?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bindings[i].stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;}
        if(rayQuery){bindings.back().descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;bindings.back().stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;}
        auto set=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        set.bindingCount=static_cast<std::uint32_t>(bindings.size());set.pBindings=bindings.data();
        check(b.f.vkCreateDescriptorSetLayout(b.device,&set,nullptr,&setLayout),"Create raster descriptors");
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(MaterialPush)};
        auto pipelineLayout=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pipelineLayout.setLayoutCount=1;pipelineLayout.pSetLayouts=&setLayout;pipelineLayout.pushConstantRangeCount=1;pipelineLayout.pPushConstantRanges=&push;
        check(b.f.vkCreatePipelineLayout(b.device,&pipelineLayout,nullptr,&layout),"Create raster pipeline layout");
        const auto fragment=shader(directory/(rayQuery?"rayquery.frag.spv":"raster.frag.spv"));
        for(int sky=0;sky<2;++sky) {
            const auto vertex=shader(directory/(sky?"sky.vert.spv":"raster.vert.spv"));
            std::array<VkPipelineShaderStageCreateInfo,2> stages{};
            for(auto& stage:stages) {stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main";}
            stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;
            VkVertexInputBindingDescription binding{0,sizeof(RasterVertex),VK_VERTEX_INPUT_RATE_VERTEX};
            std::array<VkVertexInputAttributeDescription,3> attributes{{{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(RasterVertex,position)},
                {1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(RasterVertex,normal)},{2,0,VK_FORMAT_R32G32_SFLOAT,offsetof(RasterVertex,uv)}}};
            auto input=info<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
            if(!sky){input.vertexBindingDescriptionCount=1;input.pVertexBindingDescriptions=&binding;input.vertexAttributeDescriptionCount=3;input.pVertexAttributeDescriptions=attributes.data();}
            auto assembly=info<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            auto viewport=info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);viewport.viewportCount=1;viewport.scissorCount=1;
            auto raster=info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
            raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
            auto samples=info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
            auto depth=info<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);depth.depthTestEnable=VK_TRUE;depth.depthWriteEnable=VK_TRUE;depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
            VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=0xf;
            auto color=info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);color.attachmentCount=1;color.pAttachments=&blend;
            const VkDynamicState dynamics[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
            auto dynamic=info<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamics;
            auto rendering=info<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
            rendering.colorAttachmentCount=1;rendering.pColorAttachmentFormats=&b.format;rendering.depthAttachmentFormat=VK_FORMAT_D32_SFLOAT;
            auto create=info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
            create.pNext=&rendering;create.stageCount=2;create.pStages=stages.data();create.pVertexInputState=&input;create.pInputAssemblyState=&assembly;
            create.pViewportState=&viewport;create.pRasterizationState=&raster;create.pMultisampleState=&samples;create.pDepthStencilState=&depth;
            create.pColorBlendState=&color;create.pDynamicState=&dynamic;create.layout=layout;
            check(b.f.vkCreateGraphicsPipelines(b.device,VK_NULL_HANDLE,1,&create,nullptr,sky?&skyPipeline:&pipeline),"Create Vulkan PBR pipeline");
            ++pipelineCreations;
        }
    }
};
struct RasterDraw {std::uint32_t first{},count{};MaterialPush material{};std::uint32_t asset{},instance{};MaterialData source;BoundingBox bounds;};
#include "VulkanRayQuery.inc"

// Stream the consumed inputs without allocating a second copy of large assets.
// Content checks also preserve the public CPU interface's in-place edit behavior.
struct RasterFingerprint {
    std::uint64_t value{14695981039346656037ULL};
    void bytes(const void* data,std::size_t size) {
        const auto* p=static_cast<const unsigned char*>(data);
        for(std::size_t i=0;i<size;++i){value^=p[i];value*=1099511628211ULL;}
    }
    template<class T> void add(const T& data){bytes(&data,sizeof(data));}
    template<class T> void vector(const std::vector<T>& data){add(data.size());bytes(data.data(),data.size()*sizeof(T));}
    void text(const std::string& data){add(data.size());bytes(data.data(),data.size());}
};
// Re-read every byte, but only repeat the serial FNV chain when bytes change.
// Keep this private: capture manifests retain their original independent checks.
struct RasterFileInputs {
    struct Entry {std::string bytes,fingerprint;bool used{true};};
    std::map<std::filesystem::path,Entry> entries;
    struct Frame {
        RasterFileInputs& inputs;RasterStatistics& stats;
        Frame(RasterFileInputs& in,RasterStatistics& s):inputs(in),stats(s) {
            for(auto& item:inputs.entries)item.second.used=false;
        }
        ~Frame() {
            for(auto it=inputs.entries.begin();it!=inputs.entries.end();) {
                if(!it->second.used)it=inputs.entries.erase(it);
                else {stats.fileContentCacheBytes+=it->second.bytes.capacity();++it;}
            }
        }
    };
    capture::InputFile snapshot(const std::filesystem::path& path,RasterStatistics& stats) {
        capture::InputFile result;result.path=std::filesystem::absolute(path).lexically_normal();
        result.exists=std::filesystem::is_regular_file(result.path);
        if(!result.exists){entries.erase(result.path);return result;}
#ifdef _WIN32
        // Avoid the CRT stream read overhead without retaining an open file or
        // relying on metadata. Sharing permits replacement between snapshots.
        struct File {
            HANDLE value;
            ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
        } file{CreateFileW(result.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                           nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        if(file.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot read capture input: "+result.path.string());
#else
        std::ifstream file(result.path,std::ios::binary);
        if(!file)throw std::runtime_error("Cannot read capture input: "+result.path.string());
#endif
        auto found=entries.find(result.path);bool changed=found==entries.end();
        if(!changed)found->second.used=true;
        std::string candidate;std::size_t offset=0;char block[65536];
        for(;;) {
#ifdef _WIN32
            DWORD received=0;
            if(!ReadFile(file.value,block,static_cast<DWORD>(sizeof(block)),&received,nullptr))
                throw std::runtime_error("Cannot fingerprint capture input: "+result.path.string());
            const auto count=static_cast<std::size_t>(received);
#else
            file.read(block,sizeof(block));const auto count=static_cast<std::size_t>(file.gcount());
            if(!file && !file.eof())throw std::runtime_error("Cannot fingerprint capture input: "+result.path.string());
#endif
            if(!count)break;
            stats.fileContentReadBytes+=count;
            if(!changed && (count>found->second.bytes.size()-offset || std::memcmp(found->second.bytes.data()+offset,block,count)!=0)) {
                changed=true;candidate.assign(found->second.bytes.data(),offset);
            }
            if(changed)candidate.append(block,count);
            offset+=count;
        }
        if(!changed && offset!=found->second.bytes.size()) {
            changed=true;candidate.assign(found->second.bytes.data(),offset);
        }
        result.bytes=offset;
        if(changed) {
            result.fingerprint=capture::fingerprintBytes(candidate);stats.fileContentHashBytes+=candidate.size();
            entries.insert_or_assign(result.path,Entry{std::move(candidate),result.fingerprint,true});
        } else result.fingerprint=found->second.fingerprint;
        return result;
    }
};
std::uint64_t rasterAssetKey(const pathtracer::SceneSnapshot& scene,RasterStatistics& stats,RasterFileInputs& files) {
    const auto start=std::chrono::steady_clock::now();
    RasterFingerprint key;key.add(scene.assets().size());
    for(const auto& asset:scene.assets()) {
        key.add(reinterpret_cast<std::uintptr_t>(asset.model.get()));
        const auto& model=*asset.model;key.add(model.meshes.size());
        const auto meshStart=std::chrono::steady_clock::now();
        for(const auto& mesh:model.meshes) {
            // Match RasterVertex and the position-only RQ geometry consumer.
            // Tangents and skinning fields are not read by this rigid Raster path.
            key.add(mesh.vertices.size());
            for(const auto& vertex:mesh.vertices){key.add(vertex.position);key.add(vertex.normal);key.add(vertex.texCoord0);}
            key.vector(mesh.indices);key.add(mesh.submeshes.size());
            for(const auto& part:mesh.submeshes){key.add(part.firstIndex);key.add(part.indexCount);key.add(part.materialIndex);}
        }
        stats.meshFingerprintMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-meshStart).count();
        key.add(model.materials.size());
        for(const auto& material:model.materials) {
            key.add(material.baseColorFactor);key.add(material.emissiveFactor);key.add(material.metallicFactor);key.add(material.roughnessFactor);
            key.add(material.baseColorTextureIndex);key.add(material.normalTextureIndex);key.add(material.metallicRoughnessTextureIndex);
            key.add(material.transmissionFactor);key.add(material.alphaMode);key.add(material.alphaCutoff);key.add(material.doubleSided);
        }
        const auto textureStart=std::chrono::steady_clock::now();
        key.add(model.textures.size());
        for(const auto& texture:model.textures) {
            key.add(texture.width);key.add(texture.height);key.add(texture.srgb);
            key.vector(texture.encodedData);key.vector(texture.rgbaPixels);key.text(texture.sourcePath.generic_u8string());
            const auto inputStart=std::chrono::steady_clock::now();
            capture::recordInput(texture.sourcePath);
            if(texture.rgbaPixels.empty() && texture.encodedData.empty() && !texture.sourcePath.empty())
                key.text(files.snapshot(texture.sourcePath,stats).fingerprint);
            stats.textureInputMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-inputStart).count();
        }
        stats.textureFingerprintMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-textureStart).count();
    }
    // Descriptor order follows the actual draw list, including repeated meshes.
    key.add(scene.instances().size());
    for(const auto& instance:scene.instances()){key.add(instance.assetIndex);key.add(instance.meshIndex);}
    stats.assetFingerprintMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return key.value;
}
struct RasterSceneResources {
    RasterResources resources;
    std::unique_ptr<RasterResources> uploads;
    RasterResources::Buffer uniform;
    std::vector<VkDescriptorSet> sets;
    VkAccelerationStructureKHR acceleration{};
    std::vector<pathtracer::SceneSnapshotAsset> assets; // Keep pointer identities alive.
    std::uint64_t key{};
    explicit RasterSceneResources(Bootstrap& b):resources(b),uploads(std::make_unique<RasterResources>(b)) {}
};
struct RasterGeometryResources {
    RasterResources resources;
    RasterResources::Buffer vertices;
    std::vector<RasterDraw> draws;
    std::uint64_t key{};
    explicit RasterGeometryResources(Bootstrap& b):resources(b) {}
};
struct RasterSizeResources {
    RasterResources resources;
    RasterResources::Buffer readback;
    RasterResources::Image depth;
    int width{},height{};
    bool initialized{false};
    explicit RasterSizeResources(Bootstrap& b,int w,int h):resources(b),width(w),height(h) {
        readback=resources.buffer(static_cast<VkDeviceSize>(w)*h*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        depth=resources.image(w,h,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
    }
};
}
struct VulkanRaster::Impl {
    Bootstrap b;
    std::filesystem::path directory;
    RasterStatistics stats;
    std::unique_ptr<RayQueryScene> acceleration;
    std::unique_ptr<RasterResources> pipelineResources;
    std::uint64_t pipelineKey{};
    std::unique_ptr<RasterSceneResources> sceneResources;
    std::unique_ptr<RasterGeometryResources> geometryResources;
    std::unique_ptr<RasterSizeResources> sizeResources;
    std::string environmentKey;
    std::vector<glm::vec4> cachedEnvironment;
    RasterFileInputs fileInputs;
    unsigned tick{0};
    Impl(const std::filesystem::path& shaders,bool validation):directory(shaders) {b.initialize(validation,validation,false);}
    const std::vector<glm::vec4>& environmentPixels(const pathtracer::SnapshotEnvironment& source) {
        if(source.sourcePath.empty() && source.radiancePixels.empty()) {cachedEnvironment.clear();environmentKey.clear();return cachedEnvironment;}
        std::string key;
        if(!source.sourcePath.empty()) {
            capture::recordInput(source.sourcePath);
            key=source.sourcePath.generic_string()+fileInputs.snapshot(source.sourcePath,stats).fingerprint;
        }
        if(!source.radiancePixels.empty()) {
            RasterFingerprint pixels;pixels.add(source.width);pixels.add(source.height);pixels.vector(source.radiancePixels);
            key+=std::to_string(pixels.value);
        }
        if(key==environmentKey && !cachedEnvironment.empty())return cachedEnvironment;
        auto unit=source;unit.intensity=1;
        const pathtracer::EnvironmentLight environment(unit);
        if(!environment.width() || !environment.height())throw std::runtime_error("Cannot decode shared Vulkan HDRI: "+source.sourcePath.string());
        std::vector<glm::vec4> candidate;candidate.reserve(512*256);
        for(int y=0;y<256;++y)for(int x=0;x<512;++x) {
            const float theta=3.14159265359f*(y+0.5f)/256,phi=6.28318530718f*((x+0.5f)/512-0.5f);
            candidate.emplace_back(environment.radiance({std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi)}),1);
        }
        cachedEnvironment=std::move(candidate);environmentKey=key;return cachedEnvironment;
    }
    std::vector<unsigned char> render(const pathtracer::SceneSnapshot& scene,int width,int height,float exposure,bool tone,bool cullBackFaces,bool rayQuery,bool shadowDebug,bool frustumCulling) {
        const auto start=std::chrono::steady_clock::now();
        stats={};
        RasterFileInputs::Frame fileFrame(fileInputs,stats);
        if(rayQuery && !b.rayQueryAvailable)throw std::runtime_error("Vulkan Ray Query shadows unavailable: require rayQuery, accelerationStructure and bufferDeviceAddress; disable Ray Query shadows or select OpenGL CSM");
        if(width<1 || height<1 || width>8192 || height>8192)throw std::runtime_error("Vulkan baseline resolution must be 1..8192");
        if(scene.lighting().localLights.size()>16)throw std::runtime_error("Vulkan baseline supports at most 16 local lights");
        const auto assetKey=rasterAssetKey(scene,stats,fileInputs);
        RasterFingerprint geometryKey;geometryKey.add(assetKey);
        for(const auto& instance:scene.instances()){geometryKey.add(instance.objectToWorld);geometryKey.add(instance.normalToWorld);}
        const bool replaceGeometry=!geometryResources || geometryResources->key!=geometryKey.value;
        std::vector<RasterVertex> vertices;std::vector<RasterDraw> preparedDraws;
        for(std::size_t instanceIndex=0;instanceIndex<scene.instances().size();++instanceIndex) {
            const auto& instance=scene.instances()[instanceIndex];
            if(instance.skinned || !instance.transformInvertible)throw std::runtime_error("Vulkan baseline requires static/rigid invertible meshes");
            const auto& model=*scene.assets().at(instance.assetIndex).model;
            const auto& mesh=model.meshes.at(instance.meshIndex);
            for(const auto& submesh:mesh.submeshes) {
                MaterialData material;if(submesh.materialIndex>=0)material=model.materials.at(static_cast<std::size_t>(submesh.materialIndex));
                if(material.transmissionFactor>0 || material.alphaMode==MaterialAlphaMode::Blend)throw std::runtime_error("Vulkan baseline does not support transmission or alpha blend");
                if(rayQuery && material.alphaMode!=MaterialAlphaMode::Opaque)throw std::runtime_error("Ray Query shadows require opaque static/rigid materials; Alpha Mask is unsupported");
                RasterDraw draw;draw.first=static_cast<std::uint32_t>(vertices.size());draw.count=submesh.indexCount;draw.asset=instance.assetIndex;draw.instance=static_cast<std::uint32_t>(instanceIndex);draw.source=material;
                draw.material={material.baseColorFactor*glm::vec4(instance.tint,1),glm::vec4(material.emissiveFactor,material.metallicFactor),
                    {material.roughnessFactor,material.alphaMode==MaterialAlphaMode::Mask?material.alphaCutoff:0.0f,(material.normalTextureIndex>=0?1.0f:0.0f)+(material.doubleSided?2.0f:0.0f),cullBackFaces?2.0f:0.0f}};
                if(submesh.firstIndex>mesh.indices.size() || submesh.indexCount>mesh.indices.size()-submesh.firstIndex || submesh.indexCount%3)throw std::runtime_error("Invalid shared mesh index range");
                if(replaceGeometry)for(std::size_t index=submesh.firstIndex;index<static_cast<std::size_t>(submesh.firstIndex)+submesh.indexCount;++index) {
                    const auto local=index-submesh.firstIndex,corner=local%3;
                    const auto sourceIndex=glm::determinant(glm::mat3(instance.objectToWorld))<0 && corner>0
                        ? index-corner+3-corner:index;
                    const auto& v=mesh.vertices.at(mesh.indices.at(sourceIndex));
                    const auto worldPosition=glm::vec3(instance.objectToWorld*glm::vec4(v.position,1));
                    if(index==submesh.firstIndex)draw.bounds={worldPosition,worldPosition};
                    else {draw.bounds.minimum=glm::min(draw.bounds.minimum,worldPosition);draw.bounds.maximum=glm::max(draw.bounds.maximum,worldPosition);}
                    vertices.push_back({worldPosition,glm::normalize(instance.normalToWorld*v.normal),v.texCoord0});
                }
                if(replaceGeometry)preparedDraws.push_back(draw);
            }
        }
        const auto resourcesStart=std::chrono::steady_clock::now();
        stats.geometryPreparationMilliseconds=std::chrono::duration<double,std::milli>(resourcesStart-start).count();
        if(b.extent.width!=static_cast<unsigned>(width) || b.extent.height!=static_cast<unsigned>(height)) {
            glfwSetWindowSize(b.window,width,height);glfwPollEvents();b.createSwapchain();
        }
        if(b.extent.width!=static_cast<unsigned>(width) || b.extent.height!=static_cast<unsigned>(height))throw std::runtime_error("Vulkan surface extent differs from requested output");
        stats.device=b.report.deviceName;stats.triangles=0;
        const auto& draws=replaceGeometry?preparedDraws:geometryResources->draws;
        stats.draws=1; // sky; geometry counters below describe actual submissions
        stats.presentMode=std::getenv("IRIS_VULKAN_DISABLE_VSYNC")?"immediate":"fifo";
        stats.rayQueryAvailable=b.rayQueryAvailable;stats.rayQueryShadows=rayQuery;stats.shadowDebug=rayQuery&&shadowDebug;
        std::vector<unsigned char> pixels;
        std::chrono::steady_clock::time_point releaseStart;
        const auto shaderInputStart=std::chrono::steady_clock::now();
        RasterFingerprint shaderKey;shaderKey.add(b.format);shaderKey.add(rayQuery);
        for(const auto* file:{"raster.vert.spv","sky.vert.spv",rayQuery?"rayquery.frag.spv":"raster.frag.spv"}) {
            capture::recordInput(directory/file);shaderKey.text(capture::snapshot(directory/file).fingerprint);
        }
        stats.shaderInputMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-shaderInputStart).count();
        std::unique_ptr<RasterResources> pipelineCandidate;
        if(!pipelineResources || pipelineKey!=shaderKey.value) {
            const auto pipelineStart=std::chrono::steady_clock::now();
            pipelineCandidate=std::make_unique<RasterResources>(b);pipelineCandidate->createPipeline(directory,rayQuery);
            stats.pipelineCreationMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pipelineStart).count();
        }
        auto& pipeline=pipelineCandidate?*pipelineCandidate:*pipelineResources;
        const auto pipelineCreationsBefore=pipelineCandidate?0:pipeline.pipelineCreations;
        std::unique_ptr<RasterGeometryResources> geometryCandidate;
        if(replaceGeometry) {
            geometryCandidate=std::make_unique<RasterGeometryResources>(b);
            geometryCandidate->vertices=geometryCandidate->resources.buffer(vertices.size()*sizeof(RasterVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertices.data());
            geometryCandidate->draws=preparedDraws;geometryCandidate->key=geometryKey.value;
        }
        auto& geometryCache=geometryCandidate?*geometryCandidate:*geometryResources;
        const auto geometryUploadBytes=geometryCandidate?geometryCache.resources.hostUploadBytes:0;
        const auto& lighting=scene.lighting();const auto& environment=lighting.environment;
        const auto environmentStart=std::chrono::steady_clock::now();
        const auto& panorama=environmentPixels(environment);
        stats.environmentPreparationMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-environmentStart).count();
        RasterFingerprint sceneKey;sceneKey.add(assetKey);sceneKey.add(shaderKey.value);sceneKey.text(environmentKey);
        std::unique_ptr<RasterSceneResources> sceneCandidate;
        if(!sceneResources || sceneResources->key!=sceneKey.value) {
            sceneCandidate=std::make_unique<RasterSceneResources>(b);sceneCandidate->key=sceneKey.value;
            sceneCandidate->assets=scene.assets();
            sceneCandidate->uniform=sceneCandidate->resources.buffer(sizeof(FrameUniform),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        }
        auto& sceneCache=sceneCandidate?*sceneCandidate:*sceneResources;
        auto& materialResources=sceneCache.resources;
        const auto sceneUploadBefore=sceneCandidate?0:materialResources.hostUploadBytes;
        const auto textureUploadsBefore=sceneCandidate?0:materialResources.textureUploads;
        std::unique_ptr<RasterSizeResources> sizeCandidate;
        if(!sizeResources || sizeResources->width!=width || sizeResources->height!=height)
            sizeCandidate=std::make_unique<RasterSizeResources>(b,width,height);
        auto& sizeCache=sizeCandidate?*sizeCandidate:*sizeResources;
        auto& r=sizeCache.resources;
        const auto readback=sizeCache.readback,ubo=sceneCache.uniform;
        const auto geometry=geometryCache.vertices;
        const auto depth=sizeCache.depth;
        if(rayQuery) {
            RasterFingerprint sourceKey;sourceKey.add(assetKey);
            for(const auto& instance:scene.instances())sourceKey.add(instance.castsShadow);
            RayQueryScene::prepare(b,scene,acceleration,stats,false,sourceKey.value);
        } else acceleration.reset();
        auto& frame=b.frames[tick++%b.frames.size()];
        check(b.f.vkWaitForFences(b.device,1,&frame.fence,VK_TRUE,UINT64_MAX),"Wait raster frame");
        check(b.f.vkResetCommandBuffer(frame.command,0),"Reset raster command");
        auto begin=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);check(b.f.vkBeginCommandBuffer(frame.command,&begin),"Begin raster commands");
        VkPhysicalDeviceProperties properties{};b.f.vkGetPhysicalDeviceProperties(b.physical,&properties);
        std::uint32_t queueCount=0;b.f.vkGetPhysicalDeviceQueueFamilyProperties(b.physical,&queueCount,nullptr);
        std::vector<VkQueueFamilyProperties> queueProperties(queueCount);b.f.vkGetPhysicalDeviceQueueFamilyProperties(b.physical,&queueCount,queueProperties.data());
        if(!queueProperties[b.family].timestampValidBits)throw std::runtime_error("Vulkan baseline requires graphics timestamps");
        if(pipelineCandidate) {
            auto query=info<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=4;
            check(b.f.vkCreateQueryPool(b.device,&query,nullptr,&pipeline.queries),"Create raster timestamp pool");
        }
        b.f.vkCmdResetQueryPool(frame.command,pipeline.queries,0,4);b.f.vkCmdWriteTimestamp(frame.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,pipeline.queries,0);
        FrameUniform uniform{};uniform.vp=scene.camera().projection*scene.camera().view;uniform.inverseVP=glm::inverse(uniform.vp);
        uniform.eyeExposure={scene.camera().position,exposure};uniform.sunDirectionTone={lighting.directional.direction,tone?1.0f:0.0f};
        uniform.sunRadiance={lighting.directional.radiance,shadowDebug&&rayQuery?1.0f:0.0f};uniform.background={environment.backgroundColor,1};
        uniform.environmentInfo={environment.intensity,environment.visibleToCamera?1.0f:0.0f,panorama.empty()?0.0f:1.0f,static_cast<float>(lighting.localLights.size())};
        for(std::size_t i=0;i<lighting.localLights.size();++i) {const auto& l=lighting.localLights[i];
            uniform.localPosition[i]={l.position,l.type==pathtracer::SnapshotLocalLightType::Spot?1.0f:0.0f};uniform.localDirection[i]={l.direction,l.radius};uniform.localRadiance[i]={l.radiance,l.outerConeCosine};}
        void* uniformData{};check(b.f.vkMapMemory(b.device,ubo.memory,0,sizeof(uniform),0,&uniformData),"Map raster uniforms");
        std::memcpy(uniformData,&uniform,sizeof(uniform));b.f.vkUnmapMemory(b.device,ubo.memory);
        materialResources.hostUploadBytes+=sizeof(uniform);
        stats.readbackMemoryType=readback.memoryType;stats.readbackMemoryFlags=readback.memoryFlags;
        const VkPipelineStageFlags depthStages=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        r.transition(frame.command,depth,sizeCache.initialized?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,sizeCache.initialized?depthStages:VkPipelineStageFlags(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),depthStages,
            sizeCache.initialized?VkAccessFlags(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT):0U,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        if(sceneCandidate) {
            const auto white=materialResources.texture(frame.command,1,1,{glm::vec4(1)},VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,false,sceneCache.uploads.get());
            const auto env=panorama.empty()?white:materialResources.texture(frame.command,512,256,panorama,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,false,sceneCache.uploads.get());
            std::vector<std::vector<RasterResources::Image>> assetTextures;
            for(const auto& asset:scene.assets()) {
                assetTextures.emplace_back();for(const auto& source:asset.model->textures) {
                    pathtracer::CpuTexture cpu(source);if(!cpu.valid())throw std::runtime_error("Cannot decode Vulkan material texture: "+source.name);
                    ++stats.textureDecodes;
                    std::vector<glm::vec4> texels;texels.reserve(static_cast<std::size_t>(cpu.width())*cpu.height());
                    for(std::uint32_t y=0;y<cpu.height();++y)for(std::uint32_t x=0;x<cpu.width();++x)
                        texels.push_back(cpu.sampleRepeatBilinear({(x+0.5f)/cpu.width(),(y+0.5f)/cpu.height()}));
                    assetTextures.back().push_back(materialResources.texture(frame.command,cpu.width(),cpu.height(),texels,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,true,sceneCache.uploads.get()));
                }
            }
            auto sampler=info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
            sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;sampler.maxLod=VK_LOD_CLAMP_NONE;
            sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;
            check(b.f.vkCreateSampler(b.device,&sampler,nullptr,&materialResources.sampler),"Create raster sampler");
            const auto setCount=static_cast<std::uint32_t>(draws.size()+1);
            const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,setCount},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,setCount*4},{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,setCount}};
            auto pool=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool.maxSets=setCount;pool.poolSizeCount=rayQuery?3:2;pool.pPoolSizes=sizes;
            check(b.f.vkCreateDescriptorPool(b.device,&pool,nullptr,&materialResources.descriptors),"Create raster descriptor pool");
            std::vector<VkDescriptorSetLayout> layouts(setCount,pipeline.setLayout);auto& sets=sceneCache.sets;sets.resize(setCount);
            auto allocate=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);allocate.descriptorPool=materialResources.descriptors;allocate.descriptorSetCount=setCount;allocate.pSetLayouts=layouts.data();
            check(b.f.vkAllocateDescriptorSets(b.device,&allocate,sets.data()),"Allocate raster descriptors");
            for(std::uint32_t i=0;i<setCount;++i) {
                auto base=white,mr=white,normal=white;
                if(i<draws.size()) {const auto& d=draws[i];const auto pick=[&](int index){return index<0?white:assetTextures.at(d.asset).at(static_cast<std::size_t>(index));};
                    base=pick(d.source.baseColorTextureIndex);mr=pick(d.source.metallicRoughnessTextureIndex);normal=pick(d.source.normalTextureIndex);}
                VkDescriptorBufferInfo bufferInfo{ubo.handle,0,sizeof(uniform)};
                const VkDescriptorImageInfo images[]{{materialResources.sampler,base.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{materialResources.sampler,mr.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                    {materialResources.sampler,env.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{materialResources.sampler,normal.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
                std::array<VkWriteDescriptorSet,5> writes{};
                for(std::uint32_t j=0;j<writes.size();++j) {auto& w=writes[j];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=sets[i];w.dstBinding=j;w.descriptorCount=1;
                    w.descriptorType=j?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;if(j)w.pImageInfo=&images[j-1];else w.pBufferInfo=&bufferInfo;}
                b.f.vkUpdateDescriptorSets(b.device,static_cast<std::uint32_t>(writes.size()),writes.data(),0,nullptr);
            }
        }
        auto& sets=sceneCache.sets;
        // A rejected scene replacement may free and later reuse an AS handle;
        // a fresh build must refresh descriptors even if its numeric handle matches.
        if(rayQuery && (stats.tlasBuilds || sceneCache.acceleration!=acceleration->tlas)) {
            for(std::size_t i=0;i<sets.size();++i) {
                auto value=info<VkWriteDescriptorSetAccelerationStructureKHR>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
                value.accelerationStructureCount=1;value.pAccelerationStructures=&acceleration->tlas;
                auto write=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);write.pNext=&value;write.dstSet=sets[i];write.dstBinding=5;
                write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;b.f.vkUpdateDescriptorSets(b.device,1,&write,0,nullptr);
            }
            sceneCache.acceleration=acceleration->tlas;
        }
        const auto submitStart=std::chrono::steady_clock::now();
        stats.resourcePreparationMilliseconds=std::chrono::duration<double,std::milli>(submitStart-resourcesStart).count();
        b.f.vkCmdWriteTimestamp(frame.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,pipeline.queries,1);
        std::uint32_t imageIndex=0;
        VkResult acquire=VK_ERROR_OUT_OF_DATE_KHR;
        const auto pipelineFormat=b.format;
        for(int retry=0;retry<3;++retry) {
            acquire=b.f.vkAcquireNextImageKHR(b.device,b.swapchain,UINT64_MAX,frame.acquired,VK_NULL_HANDLE,&imageIndex);
            if(acquire!=VK_ERROR_OUT_OF_DATE_KHR)break;
            b.createSwapchain();
            if(b.format!=pipelineFormat || b.extent.width!=static_cast<unsigned>(width) || b.extent.height!=static_cast<unsigned>(height))
                throw std::runtime_error("Vulkan surface changed during acquisition; retry the frame with new resources");
        }
        if(acquire!=VK_SUBOPTIMAL_KHR)check(acquire,"Acquire raster image");
        const RasterResources::Image target{b.images[imageIndex],VK_NULL_HANDLE,b.views[imageIndex]};
        r.transition(frame.command,target,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        if(b.f.vkCmdBeginDebugUtilsLabelEXT) {auto label=info<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);label.pLabelName="Iris.VulkanRaster.PBR";b.f.vkCmdBeginDebugUtilsLabelEXT(frame.command,&label);}
        auto color=info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);color.imageView=target.view;color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        auto depthAttachment=info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);depthAttachment.imageView=depth.view;depthAttachment.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;depthAttachment.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;depthAttachment.clearValue.depthStencil={1,0};
        auto rendering=info<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);rendering.renderArea.extent=b.extent;rendering.layerCount=1;rendering.colorAttachmentCount=1;rendering.pColorAttachments=&color;rendering.pDepthAttachment=&depthAttachment;
        b.f.vkCmdBeginRendering(frame.command,&rendering);
        const VkViewport viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};const VkRect2D scissor{{0,0},b.extent};
        b.f.vkCmdSetViewport(frame.command,0,1,&viewport);b.f.vkCmdSetScissor(frame.command,0,1,&scissor);
        const MaterialPush sky{{1,1,1,1},{0,0,0,0},{1,0,0,1}};
        b.f.vkCmdBindPipeline(frame.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline.skyPipeline);
        b.f.vkCmdBindDescriptorSets(frame.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline.layout,0,1,&sets.back(),0,nullptr);
        b.f.vkCmdPushConstants(frame.command,pipeline.layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(sky),&sky);b.f.vkCmdDraw(frame.command,3,1,0,0);
        b.f.vkCmdBindPipeline(frame.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline.pipeline);const VkDeviceSize offset=0;b.f.vkCmdBindVertexBuffers(frame.command,0,1,&geometry.handle,&offset);
        // The vertex shader converts OpenGL clip Z to Vulkan Z after frame.vp.
        // Keep the same [-w,w] test here; AS membership is deliberately untouched.
        for(std::size_t i=0;i<draws.size();++i) {const auto& d=draws[i];
            ++stats.visibility.submittedDraws;stats.visibility.submittedTriangles+=d.count/3;
            if(frustumCulling && !intersectsClipBox(d.bounds,uniform.vp)){++stats.visibility.culledDraws;continue;}
            ++stats.draws;stats.triangles+=d.count/3;stats.visibility.drawnTriangles+=d.count/3;
            b.f.vkCmdBindDescriptorSets(frame.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline.layout,0,1,&sets[i],0,nullptr);
            auto material=d.material;material.base=d.source.baseColorFactor*glm::vec4(scene.instances().at(d.instance).tint,1);
            material.parameters.w=cullBackFaces?2.0f:0.0f;
            b.f.vkCmdPushConstants(frame.command,pipeline.layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(material),&material);b.f.vkCmdDraw(frame.command,d.count,1,d.first,0);}
        b.f.vkCmdEndRendering(frame.command);if(b.f.vkCmdEndDebugUtilsLabelEXT)b.f.vkCmdEndDebugUtilsLabelEXT(frame.command);
        b.f.vkCmdWriteTimestamp(frame.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,pipeline.queries,2);
        r.transition(frame.command,target,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={static_cast<unsigned>(width),static_cast<unsigned>(height),1};
        b.f.vkCmdCopyImageToBuffer(frame.command,target.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,&copy);
        r.transition(frame.command,target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,VK_ACCESS_TRANSFER_READ_BIT,0);
        b.f.vkCmdWriteTimestamp(frame.command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,pipeline.queries,3);check(b.f.vkEndCommandBuffer(frame.command),"End raster commands");
        const VkPipelineStageFlags wait=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&frame.acquired;submit.pWaitDstStageMask=&wait;
        submit.commandBufferCount=1;submit.pCommandBuffers=&frame.command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&b.presentReady[imageIndex];
        check(b.f.vkResetFences(b.device,1,&frame.fence),"Reset raster fence");check(b.f.vkQueueSubmit(b.queue,1,&submit,frame.fence),"Submit raster frame");
        auto present=info<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);present.waitSemaphoreCount=1;present.pWaitSemaphores=&b.presentReady[imageIndex];present.swapchainCount=1;present.pSwapchains=&b.swapchain;present.pImageIndices=&imageIndex;
        const auto presented=b.f.vkQueuePresentKHR(b.queue,&present);if(presented!=VK_SUBOPTIMAL_KHR && presented!=VK_ERROR_OUT_OF_DATE_KHR)check(presented,"Present raster frame");
        check(b.f.vkWaitForFences(b.device,1,&frame.fence,VK_TRUE,UINT64_MAX),"Wait raster readback");
        const auto readbackStart=std::chrono::steady_clock::now();
        stats.submitWaitMilliseconds=std::chrono::duration<double,std::milli>(readbackStart-submitStart).count();
        pixels.resize(static_cast<std::size_t>(width)*height*4);void* mapped{};check(b.f.vkMapMemory(b.device,readback.memory,0,pixels.size(),0,&mapped),"Map raster image");
        std::memcpy(pixels.data(),mapped,pixels.size());b.f.vkUnmapMemory(b.device,readback.memory);
        if(b.format==VK_FORMAT_B8G8R8A8_SRGB)for(std::size_t i=0;i<pixels.size();i+=4)std::swap(pixels[i],pixels[i+2]);
        stats.readbackMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-readbackStart).count();
        const auto* uploads=sceneCache.uploads.get();
        stats.hostUploadBytes=materialResources.hostUploadBytes-sceneUploadBefore+geometryUploadBytes+(uploads?uploads->hostUploadBytes:0);stats.readbackBytes=pixels.size();
        stats.textureUploads=materialResources.textureUploads-textureUploadsBefore;
        stats.pipelineCreations=pipeline.pipelineCreations-pipelineCreationsBefore;
        std::uint64_t timestamps[4]{};check(b.f.vkGetQueryPoolResults(b.device,pipeline.queries,0,4,sizeof(timestamps),timestamps,sizeof(std::uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"Read raster timestamps");
        const auto bits=queueProperties[b.family].timestampValidBits;const auto mask=bits==64?UINT64_MAX:((1ULL<<bits)-1);
        const auto elapsedGpu=[&](unsigned first,unsigned last){return static_cast<double>((timestamps[last]-timestamps[first])&mask)*properties.limits.timestampPeriod/1e6;};
        stats.uploadGpuMilliseconds=elapsedGpu(0,1);stats.drawGpuMilliseconds=elapsedGpu(1,2);stats.readbackGpuMilliseconds=elapsedGpu(2,3);
        stats.gpuMilliseconds=elapsedGpu(0,3)+stats.accelerationGpuMilliseconds;
        // Include upload storage in the cold frame's allocation snapshot, even
        // though it is released below; warm frames report only retained storage.
        stats.allocatedBytes=r.allocatedBytes+pipeline.allocatedBytes+materialResources.allocatedBytes+geometryCache.resources.allocatedBytes+(acceleration?acceleration->storage.allocatedBytes:0)+(uploads?uploads->allocatedBytes:0);
        stats.deviceLocalBytes=r.deviceLocalBytes+pipeline.deviceLocalBytes+materialResources.deviceLocalBytes+geometryCache.resources.deviceLocalBytes+(acceleration?acceleration->storage.deviceLocalBytes:0)+(uploads?uploads->deviceLocalBytes:0);stats.swapchainPayloadEstimateBytes=static_cast<std::uint64_t>(width)*height*4*b.images.size();
        if(presented==VK_ERROR_OUT_OF_DATE_KHR || presented==VK_SUBOPTIMAL_KHR || acquire==VK_SUBOPTIMAL_KHR)b.createSwapchain();
        if(b.errors.load())throw std::runtime_error("Vulkan raster validation reported errors");
        sizeCache.initialized=true;
        releaseStart=std::chrono::steady_clock::now();
        // Submission and readback have completed. Staging is never needed by a
        // later draw; failed candidates still keep its original RAII protection.
        sceneCache.uploads.reset();
        // Publish candidates only after completed submission/readback. Old resources
        // stay usable if validation, shader loading or allocation rejects a candidate.
        if(sceneCandidate)sceneResources=std::move(sceneCandidate);
        if(geometryCandidate)geometryResources=std::move(geometryCandidate);
        if(sizeCandidate)sizeResources=std::move(sizeCandidate);
        if(pipelineCandidate){pipelineResources=std::move(pipelineCandidate);pipelineKey=shaderKey.value;}
        stats.resourceReleaseMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-releaseStart).count();
        stats.validationEnabled=b.report.validationEnabled;stats.synchronizationValidation=b.report.synchronizationValidation;
        stats.dispatchBlasBuilds=stats.blasBuilds;stats.dispatchTlasBuilds=stats.tlasBuilds;stats.dispatchTlasUpdates=stats.tlasUpdates;
        stats.validationWarnings=b.warnings.load();stats.validationErrors=b.errors.load();
        if(stats.validationErrors)throw std::runtime_error("Vulkan raster validation reported errors");
        stats.cpuMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return pixels;
    }
};
VulkanRaster::VulkanRaster(const std::filesystem::path& directory,bool validation):impl_(std::make_unique<Impl>(directory,validation)) {}
VulkanRaster::~VulkanRaster()=default;
std::vector<unsigned char> VulkanRaster::render(const pathtracer::SceneSnapshot& scene,int width,int height,float exposure,bool tone,bool cullBackFaces,bool rayQuery,bool debug,bool frustumCulling) {return impl_->render(scene,width,height,exposure,tone,cullBackFaces,rayQuery,debug,frustumCulling);}
const RasterStatistics& VulkanRaster::statistics() const {return impl_->stats;}
}
