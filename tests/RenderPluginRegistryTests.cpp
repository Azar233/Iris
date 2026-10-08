#include "plugin/RenderPluginRegistry.h"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::exception& error) {
        rejected = std::string(error.what()).find(message) != std::string::npos;
    }
    require(rejected, "Missing expected plugin diagnostic");
}
class FakePlugin final : public iris::RenderPlugin {
public:
    explicit FakePlugin(int& live) : live_(live) { ++live_; }
    ~FakePlugin() override { --live_; }
    void renderFrame(const iris::RenderPluginFrame&) override {}
    void invalidateHistory() override {}
    iris::RenderPluginFrameInfo frameInfo() const override { return {}; }
private:
    int& live_;
};
}

int main() {
    try {
        iris::RenderPluginRegistry registry;
        int live = 0;
        int factoryCalls = 0;
        const auto factory = [&](const std::filesystem::path& directory) {
            require(directory == "test-shaders", "Factory resource root changed");
            ++factoryCalls;
            return std::make_unique<FakePlugin>(live);
        };
        registry.add({"test", iris::renderPluginApiVersion, {"gpu"}, "MIT"}, factory);
        rejects([&] { registry.add({"test", iris::renderPluginApiVersion, {}, "MIT"}, factory); }, "Duplicate");
        rejects([&] { registry.add({"wrong-api", 99, {}, "MIT"}, factory); }, "API");
        rejects([&] { registry.add({"", iris::renderPluginApiVersion, {}, "MIT"}, factory); }, "ID");
        rejects([&] { registry.add({"empty-service", iris::renderPluginApiVersion, {""}, "MIT"}, factory); }, "service");
        rejects([&] { registry.add({"empty-factory", iris::renderPluginApiVersion, {}, "MIT"}, {}); }, "factory");
        require(registry.size() == 1, "Rejected registration changed registry");
        rejects([&] { registry.create("missing", {"gpu"}, "test-shaders"); }, "unavailable");
        rejects([&] { registry.create("test", {}, "test-shaders"); }, "requires service");
        require(factoryCalls == 0, "Missing dependency allocated resources");
        { auto plugin = registry.create("test", {"gpu"}, "test-shaders");
          require(live == 1 && factoryCalls == 1, "Valid factory not called exactly once"); }
        require(live == 0, "Plugin instance did not release resources");
        registry.add({"fails", iris::renderPluginApiVersion, {}, "MIT"}, [](const std::filesystem::path&) -> std::unique_ptr<iris::RenderPlugin> {
            throw std::runtime_error("compile failed");
        });
        rejects([&] { registry.create("fails", {}, "test-shaders"); }, "compile failed");
        registry.add({"null", iris::renderPluginApiVersion, {}, "MIT"}, [](const std::filesystem::path&) -> std::unique_ptr<iris::RenderPlugin> { return {}; });
        rejects([&] { registry.create("null", {}, "test-shaders"); }, "no instance");
        { auto plugin = registry.create("test", {"gpu"}, "test-shaders");
          require(live == 1, "Factory failure damaged valid plugin"); }
        require(live == 0, "Final plugin resource leak");
        std::cout << "Render plugin registry: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
