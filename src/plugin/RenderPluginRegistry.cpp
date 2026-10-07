#include "plugin/RenderPluginRegistry.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace iris {

void RenderPluginRegistry::add(RenderPluginDescriptor descriptor, Factory factory) {
    if (descriptor.id.empty() || !factory)
        throw std::invalid_argument("Render plugin requires an ID and factory");
    if (descriptor.apiVersion != renderPluginApiVersion)
        throw std::invalid_argument("Unsupported render plugin API: " + descriptor.id);
    if (contains(descriptor.id))
        throw std::invalid_argument("Duplicate render plugin ID: " + descriptor.id);
    for (const auto& service : descriptor.requiredServices)
        if (service.empty()) throw std::invalid_argument("Empty render plugin service: " + descriptor.id);
    entries_.push_back({std::move(descriptor), std::move(factory)});
}

bool RenderPluginRegistry::contains(const std::string& id) const {
    return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.descriptor.id == id;
    });
}

std::unique_ptr<RenderPlugin> RenderPluginRegistry::create(const std::string& id,
    const std::vector<std::string>& services, const std::filesystem::path& shaderDirectory) const {
    const auto entry = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& value) {
        return value.descriptor.id == id;
    });
    if (entry == entries_.end()) throw std::runtime_error("Render plugin unavailable: " + id);
    for (const auto& required : entry->descriptor.requiredServices)
        if (std::find(services.begin(), services.end(), required) == services.end())
            throw std::runtime_error("Render plugin " + id + " requires service: " + required);
    auto instance = entry->factory(shaderDirectory);
    if (!instance) throw std::runtime_error("Render plugin factory returned no instance: " + id);
    return instance;
}

} // namespace iris
