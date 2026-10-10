#include "plugin/RenderPluginConfiguration.h"
#include "plugin/RenderPluginRegistry.h"
#include "render/Renderer.h"
#include "scene/SceneDocument.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
ModuleParameterOverride number(const std::string& id, float value) {
    ModuleParameterOverride entry; entry.id = id; entry.value.type = ModuleParameterType::Float;
    entry.value.number = value; return entry;
}
}

int main() {
    try {
        const auto* ocean = iris::builtinRenderPluginParameterSchema(iris::enscapePluginId);
        const auto* post = iris::builtinRenderPluginParameterSchema(iris::postProcessPluginId);
        require(ocean && post, "Both plugin schemas must be available without GPU allocations");
        iris::RenderPluginRegistry registry;
        auto factory = [](const std::filesystem::path&) -> std::unique_ptr<iris::RenderPlugin> { return {}; };
        registry.add({iris::enscapePluginId, iris::renderPluginApiVersion, {}, "test", {}, ocean}, factory);
        registry.add({iris::postProcessPluginId, iris::renderPluginApiVersion, {}, "test", {}, post}, factory);
        RendererSettings settings;
        bool history = false; std::string error;
        require(iris::setPluginParameter(registry, settings, iris::enscapePluginId, number("waveHeight", 0.9f), history, error)
            && history && settings.enscapeCube.waveHeight == 0.9f, "Ocean edit did not update shared settings/history");
        require(iris::setPluginParameter(registry, settings, iris::postProcessPluginId, number("exposure", 1.7f), history, error)
            && !history && settings.exposure == 1.7f, "Post history policy or settings bridge broken");
        for (auto bad : {number("missing", 1), number("waveHeight", 7),
                number("waveHeight", std::numeric_limits<float>::quiet_NaN())}) {
            require(!iris::setPluginParameter(registry, settings, iris::enscapePluginId, bad, history, error)
                && !history && settings.enscapeCube.waveHeight == 0.9f, "Invalid edit mutated canonical settings");
        }
        auto badType = number("waveHeight", 1); badType.value.type = ModuleParameterType::Bool;
        require(!iris::setPluginParameter(registry, settings, iris::enscapePluginId, badType, history, error), "Wrong type accepted");
        require(!ocean->apply(settings, {number("waveHeight", 0.5f), number("missing", 1)}, history, error)
            && settings.enscapeCube.waveHeight == 0.9f, "Partial schema apply escaped transaction");
        require(!ocean->apply(settings, {number("waveHeight", 0.5f), number("waveHeight", 0.7f)}, history, error), "Duplicate accepted");
        settings.renderPlugins = {{iris::enscapePluginId, false}};
        require(!iris::setPluginParameter(registry, settings, iris::enscapePluginId, number("waveHeight", 0.6f), history, error), "Disabled plugin edited");
        require(!iris::setPluginParameter(registry, settings, "missing", number("waveHeight", 0.6f), history, error), "Missing plugin edited");
        settings.renderPlugins.clear();
        // The legacy renderer controls modify the same fields; no stored overrides can shadow them.
        settings.exposure = 2.1f;
        const auto captured = post->capture(settings);
        require(captured[2].value.number == 2.1f, "Legacy edit not visible through schema");

        iris::RenderPluginParameterSchema typed;
        typed.pluginId = "test.typed";
        typed.metadata.registerInt("bands", "Bands", 4, 2, 8);
        typed.metadata.registerColor("tint", "Tint", glm::vec3(1));
        typed.metadata.registerEnum("preset", "Preset", {"first", "second"}, 0);
        typed.bindings = {
            {"bands", [](const RendererSettings& s) { ModuleParameterValue v; v.type=ModuleParameterType::Int; v.integer=s.stylizedBandCount; return v; },
                [](RendererSettings& s, const ModuleParameterValue& v) { s.stylizedBandCount=v.integer; }, true},
            {"tint", [](const RendererSettings& s) { ModuleParameterValue v; v.type=ModuleParameterType::Color; v.color=s.baseColor; return v; },
                [](RendererSettings& s, const ModuleParameterValue& v) { s.baseColor=v.color; }, true},
            {"preset", [](const RendererSettings&) { ModuleParameterValue v; v.type=ModuleParameterType::Enum; v.integer=0; v.text="first"; return v; },
                [](RendererSettings& s, const ModuleParameterValue& v) { s.environmentPreset=v.integer; }, true}};
        typed.validate();
        auto typedValues = typed.capture(settings);
        typedValues[0].value.integer=6; typedValues[1].value.color=glm::vec3(0.3f);
        typedValues[2].value.integer=1; typedValues[2].value.text="second";
        require(typed.apply(settings, typedValues, history, error) && settings.stylizedBandCount==6
            && settings.baseColor==glm::vec3(0.3f) && settings.environmentPreset==1, "Int/Color/Enum bindings failed");
        typedValues[0].value.integer=9;
        require(!typed.apply(settings, typedValues, history, error) && settings.stylizedBandCount==6, "Int silently clamped");
        typedValues[0].value.integer=4; typedValues[1].value.color.x=std::numeric_limits<float>::infinity();
        require(!typed.apply(settings, typedValues, history, error) && settings.stylizedBandCount==6, "Color partially committed");
        typedValues[1].value.color=glm::vec3(0.3f); typedValues[2].value.text="unknown";
        require(!typed.apply(settings, typedValues, history, error), "Unknown enum label accepted");
        typedValues[2].value.text="second";
        typed.bindings[2].write=[](RendererSettings&, const ModuleParameterValue&) { throw std::runtime_error("callback rejected"); };
        require(!typed.apply(settings, typedValues, history, error) && settings.stylizedBandCount==6
            && settings.environmentPreset==1, "Binding exception escaped settings transaction");
        auto badSchema = typed; badSchema.bindings[1].id="bands";
        bool schemaRejected=false;
        try { badSchema.validate(); } catch (const std::invalid_argument&) { schemaRejected=true; }
        require(schemaRejected, "Duplicate schema binding accepted");

        const auto directory = std::filesystem::temp_directory_path() / "IrisPluginParameterAcceptance";
        std::filesystem::create_directories(directory);
        SceneDocument scene; scene.renderer = settings;
        const auto saved = directory / "scene.myscene";
        require(saveSceneDocument(saved, scene, error), error.c_str());
        SceneDocument restored;
        require(loadSceneDocument(saved, restored, error) && restored.renderer.exposure == 2.1f
            && restored.renderer.enscapeCube.waveHeight == 0.9f, "Parameter round trip failed");
        const auto repeated = directory / "repeated.myscene";
        require(saveSceneDocument(repeated, restored, error), error.c_str());
        auto bytes = [](const auto& path) { std::ifstream stream(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(stream), {}); };
        require(bytes(saved) == bytes(repeated), "Parameter serialization not deterministic");
        const std::string prefix = "{\"format\":\"MyRendererScene\",\"version\":2,\"entities\":[],\"renderer\":{\"renderPluginParameters\":";
        for (const std::string& invalid : {
                std::string("{\"version\":2,\"entries\":[]}"),
                std::string("{\"version\":1,\"entries\":[{\"id\":\"missing\",\"schemaVersion\":1,\"values\":[]}]}"),
                std::string("{\"version\":1,\"entries\":[{\"id\":\"iris.postprocess\",\"schemaVersion\":2,\"values\":[]}]}"),
                std::string("{\"version\":1,\"entries\":[{\"id\":\"iris.postprocess\",\"schemaVersion\":1,\"values\":[{\"id\":\"exposure\",\"type\":2,\"value\":false}]}]}"),
                std::string("{\"version\":1,\"entries\":[{\"id\":\"iris.enscape-study\",\"schemaVersion\":1,\"values\":[{\"id\":\"waveHeight\",\"type\":2,\"value\":7}]}]}")}) {
            const auto path = directory / "bad.myscene";
            { std::ofstream stream(path); stream << prefix << invalid << "}}"; }
            require(!loadSceneDocument(path, restored, error) && restored.renderer.exposure == 2.1f
                && restored.renderer.enscapeCube.waveHeight == 0.9f, "Failed parameter load replaced scene");
        }
        { std::ofstream stream(directory / "legacy.myscene"); stream << "{\"format\":\"MyRendererScene\",\"version\":2,\"entities\":[],\"renderer\":{\"exposure\":1.4,\"enscapeWaveHeight\":0.7}}"; }
        require(loadSceneDocument(directory / "legacy.myscene", restored, error)
            && restored.renderer.exposure == 1.4f && restored.renderer.enscapeCube.waveHeight == 0.7f, "Legacy fields rejected");
        { std::ofstream stream(directory / "priority.myscene"); stream << "{\"format\":\"MyRendererScene\",\"version\":2,\"entities\":[],\"renderer\":{\"exposure\":1.4,\"renderPluginParameters\":{\"version\":1,\"entries\":[{\"id\":\"iris.postprocess\",\"schemaVersion\":1,\"values\":[{\"id\":\"exposure\",\"type\":2,\"value\":2.5}]}]}}}"; }
        require(loadSceneDocument(directory / "priority.myscene", restored, error) && restored.renderer.exposure==2.5f,
            "Generic values did not take precedence over legacy fields");
        auto invalidScene=restored; invalidScene.renderer.enscapeCube.waveHeight=7;
        const auto preservedBytes=bytes(saved);
        require(!saveSceneDocument(saved, invalidScene, error) && bytes(saved)==preservedBytes, "Failed parameter save truncated file");
        std::cout << "Plugin parameters: shared values, history, edit/load transactions and roundtrip PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
