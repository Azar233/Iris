#include "plugin/RenderPluginContract.h"
#include <map>
#include <set>
#include <stdexcept>

namespace iris {
void validatePluginContract(const RenderPluginContract& contract) {
    std::map<std::string, PluginResource> resources;
    std::set<std::string> ready, written, passNames;
    for (const auto& resource : contract.resources) {
        if (resource.name.empty() || !resources.emplace(resource.name, resource).second)
            throw std::invalid_argument("Duplicate/empty plugin resource");
        if (resource.scope == ResourceScope::Input || resource.scope == ResourceScope::Generated) ready.insert(resource.name);
    }
    for (const auto& pass : contract.passes) {
        if (pass.name.empty() || !passNames.insert(pass.name).second)
            throw std::invalid_argument("Duplicate/empty plugin pass");
        for (const auto& read : pass.reads) {
            const auto resource = resources.find(read);
            if (resource == resources.end() || !ready.count(read)
                || resource->second.scope == ResourceScope::History)
                throw std::invalid_argument("Unavailable same-frame plugin read: " + read);
        }
        for (const auto& read : pass.previousFrameReads) {
            const auto resource = resources.find(read);
            if (resource == resources.end() || resource->second.scope != ResourceScope::History)
                throw std::invalid_argument("Invalid plugin history read: " + read);
        }
        for (const auto& write : pass.writes) {
            const auto resource = resources.find(write);
            if (resource == resources.end() || resource->second.scope == ResourceScope::Input
                || resource->second.scope == ResourceScope::Generated
                || !written.insert(write).second)
                throw std::invalid_argument("Invalid/duplicate plugin writer: " + write);
            ready.insert(write);
        }
    }
    for (const auto& resource : contract.resources)
        if (resource.scope != ResourceScope::Input && resource.scope != ResourceScope::Generated && !written.count(resource.name))
            throw std::invalid_argument("Unproduced plugin resource: " + resource.name);
}

void validatePluginBindings(const RenderPluginContract& contract,
    const std::vector<RenderTextureBinding>& bindings, int width, int height) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("Invalid plugin frame size");
    std::map<std::string, RenderTextureBinding> bound;
    for (const auto& binding : bindings)
        if (!bound.emplace(binding.name, binding).second)
            throw std::invalid_argument("Duplicate plugin texture binding: " + binding.name);
    for (const auto& binding : bindings) {
        bool declared = false;
        for (const auto& resource : contract.resources)
            if (resource.name == binding.name && (resource.scope == ResourceScope::Input || resource.scope == ResourceScope::Output)) declared = true;
        if (!declared) throw std::invalid_argument("Undeclared plugin texture binding: " + binding.name);
    }
    std::set<unsigned int> inputTextures;
    for (const auto& resource : contract.resources) {
        if (resource.scope != ResourceScope::Input && resource.scope != ResourceScope::Output) continue;
        const auto binding = bound.find(resource.name);
        if (binding == bound.end() || binding->second.texture == 0U) {
            if (resource.required) throw std::invalid_argument("Missing plugin texture: " + resource.name);
            continue;
        }
        const bool fullSize = binding->second.width == width && binding->second.height == height;
        const bool halfSize = resource.allowsHalfResolution && binding->second.width == (width+1)/2
            && binding->second.height == (height+1)/2;
        if (binding->second.format != resource.format || (!fullSize && !halfSize))
            throw std::invalid_argument("Plugin texture format/size mismatch: " + resource.name);
        if (resource.scope == ResourceScope::Input) inputTextures.insert(binding->second.texture);
    }
    for (const auto& resource : contract.resources) {
        if (resource.scope != ResourceScope::Output) continue;
        const auto binding = bound.find(resource.name);
        if (binding != bound.end() && inputTextures.count(binding->second.texture))
            throw std::invalid_argument("Plugin input/output texture alias: " + resource.name);
    }
}

RenderPluginContract enscapeContract() {
    return {PluginStage::FullscreenScene,
        {{"weather",TextureFormat::Data,ResourceScope::Generated,true},
         {"noiseVolume",TextureFormat::Data,ResourceScope::Generated,true},
         {"ocean",TextureFormat::HdrColor,ResourceScope::Transient,true},
         {"bloom",TextureFormat::HdrColor,ResourceScope::Transient,true},
         {"history",TextureFormat::HdrColor,ResourceScope::History,true},
         {"taa",TextureFormat::HdrColor,ResourceScope::Transient,true},
         {"display",TextureFormat::DisplayColor,ResourceScope::Output,true}},
        {{"ocean",{"weather","noiseVolume"}, {"ocean"},{}}, {"bloom",{"ocean"},{"bloom"},{}},
         {"taa",{"bloom"},{"taa","history"},{"history"}},
         {"image",{"taa"},{"display"},{}}}};
}
RenderPluginContract postProcessContract() {
    return {PluginStage::PostProcess,
        {{"hdr",TextureFormat::HdrColor,ResourceScope::Input,true},
         {"depth",TextureFormat::Depth,ResourceScope::Input,false},
         {"motion",TextureFormat::Data,ResourceScope::Input,false},
         {"normals",TextureFormat::Data,ResourceScope::Input,false},
         {"cloud",TextureFormat::HdrColor,ResourceScope::Input,false,true},
         {"cloudDepth",TextureFormat::Data,ResourceScope::Input,false,true},
         {"rays",TextureFormat::Data,ResourceScope::Input,false,true},
         {"opaqueDepth",TextureFormat::Depth,ResourceScope::Input,false},
         {"gradingLut",TextureFormat::Data,ResourceScope::Generated,true},
         {"history",TextureFormat::HdrColor,ResourceScope::History,true},
         {"resolved",TextureFormat::HdrColor,ResourceScope::Transient,true},
         {"bloom",TextureFormat::HdrColor,ResourceScope::Transient,true},
         {"display",TextureFormat::DisplayColor,ResourceScope::Output,true}},
        {{"temporal",{"hdr","depth","motion"},{"resolved","history"},{"history"}},
         {"bloom",{"resolved"},{"bloom"},{}},
         {"composite",{"resolved","bloom","depth","normals","cloud","cloudDepth","rays","opaqueDepth","gradingLut"},{"display"},{}}}};
}
} // namespace iris
