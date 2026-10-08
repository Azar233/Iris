#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "plugin/RenderPlugin.h"

namespace iris {

struct RenderPluginDescriptor {
    RenderPluginDescriptor(std::string idValue, int api, std::vector<std::string> services,
        std::string licenseValue, RenderPluginContract contractValue = {})
        : id(std::move(idValue)), apiVersion(api), requiredServices(std::move(services)),
          license(std::move(licenseValue)), contract(std::move(contractValue)) {}
    std::string id;
    int apiVersion{renderPluginApiVersion};
    std::vector<std::string> requiredServices;
    std::string license;
    RenderPluginContract contract;
};

class RenderPluginRegistry {
public:
    using Factory = std::function<std::unique_ptr<RenderPlugin>(const std::filesystem::path&)>;
    void add(RenderPluginDescriptor descriptor, Factory factory);
    bool contains(const std::string& id) const;
    const RenderPluginDescriptor* descriptor(const std::string& id) const;
    std::unique_ptr<RenderPlugin> create(const std::string& id,
        const std::vector<std::string>& services, const std::filesystem::path& shaderDirectory) const;
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry { RenderPluginDescriptor descriptor; Factory factory; };
    std::vector<Entry> entries_;
};

// Registration only; querying this registry never allocates GPU resources.
const RenderPluginRegistry& builtinRenderPlugins();

} // namespace iris
