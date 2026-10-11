#include "plugin/RenderPluginConfiguration.h"
#include "plugin/RenderPluginRegistry.h"
#include "render/Renderer.h"
#include "scene/SceneDocument.h"
#include "StoredPluginFixture.h"
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
        bool noContextRejected=false;
        try { registry.prepareReplacement(iris::postProcessPluginId,{}, {},settings,320,180); }
        catch(const std::runtime_error& exception) { noContextRejected=std::string(exception.what()).find("OpenGL context")!=std::string::npos; }
        require(noContextRejected,"CPU caller entered GPU resource preparation without a context");
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
        // A new schema travels through the same commands and Scene codec without
        // adding renderer fields or compiling its GPU factory into the core.
        auto catalog = iris::builtinRenderPluginParameterCatalog();
        auto storedSchema = storedPluginSchema();
        catalog.add(storedSchema);
        catalog.add(storedPluginSchema("test.other"));
        const auto* stableSchema = catalog.find("test.stored");
        bool duplicateRejected = false;
        try { catalog.add(storedSchema); } catch (const std::invalid_argument&) { duplicateRejected = true; }
        require(duplicateRejected && stableSchema == catalog.find("test.stored"), "Catalog lost stable schema or accepted duplicate");
        iris::RenderPluginRegistry extended;
        extended.add({"test.stored", iris::renderPluginApiVersion, {}, "MIT", {}, stableSchema}, factory);
        RendererSettings independent;
        require(stableSchema->capture(independent).size() == 5 && independent.renderPluginValues.empty(), "Defaults allocated a second value source");
        require(iris::setPluginParameter(extended, independent, "test.stored", number("gain", 1.5f), history, error)
            && history && independent.exposure == RendererSettings{}.exposure, "Unbound command changed a legacy field");
        auto edits = stableSchema->capture(independent);
        edits[0].value.boolean = false; edits[2].value.integer = 6;
        edits[3].value.color = glm::vec3(0.2f, 0.4f, 0.6f);
        edits[4].value.integer = 1; edits[4].value.text = "inverted";
        require(stableSchema->apply(independent, edits, history, error), "Unbound typed transaction rejected");
        auto* other = catalog.find("test.other");
        require(other->apply(independent, {number("gain", 0.5f)}, history, error)
            && stableSchema->capture(independent)[1].value.number == 1.5f, "Plugin values were not isolated");
        SceneDocument independentScene; independentScene.renderer = independent;
        const auto independentPath = directory / "independent.myscene";
        require(saveSceneDocument(independentPath, independentScene, error, catalog), error.c_str());
        SceneDocument independentRestored;
        require(loadSceneDocument(independentPath, independentRestored, error, catalog), error.c_str());
        require(stableSchema->capture(independentRestored.renderer)[4].value.text == "inverted"
            && other->capture(independentRestored.renderer)[1].value.number == 0.5f, "Independent values did not survive Scene");
        require(saveSceneDocument(repeated, independentRestored, error, catalog)
            && bytes(independentPath) == bytes(repeated), "Custom catalog serialization was not deterministic");
        for (const auto& values : {std::string("[{\"id\":\"enabled\",\"type\":0,\"value\":true},{\"id\":\"gain\",\"type\":2,\"value\":9}]"),
                std::string("[{\"id\":\"preset\",\"type\":4,\"value\":\"missing\"}]"),
                std::string("[{\"id\":\"gain\",\"type\":2,\"value\":1.2},{\"id\":\"gain\",\"type\":2,\"value\":1.5}]"),
                std::string("[{\"id\":\"tint\",\"type\":3,\"value\":[0.1,0.2]}]")}) {
            { std::ofstream stream(directory / "bad-stored.myscene"); stream << prefix
                << "{\"version\":1,\"entries\":[{\"id\":\"test.stored\",\"schemaVersion\":1,\"values\":" << values << "}]}}}"; }
            require(!loadSceneDocument(directory / "bad-stored.myscene", independentRestored, error, catalog)
                && !stableSchema->capture(independentRestored.renderer)[0].value.boolean
                && stableSchema->capture(independentRestored.renderer)[1].value.number == 1.5f,
                "Custom parameter parse failure replaced existing scene values");
        }
        require(!loadSceneDocument(independentPath, restored, error) && restored.renderer.exposure == 2.5f,
            "Missing custom schema silently discarded values or replaced destination");
        const auto independentBytes = bytes(independentPath);
        for (int kind = 0; kind < 5; ++kind) {
            auto invalid = independentScene;
            if (kind == 0) invalid.renderer.renderPluginValues[0].pluginId = "unknown";
            if (kind == 1) invalid.renderer.renderPluginValues.push_back(invalid.renderer.renderPluginValues[0]);
            if (kind == 2) invalid.renderer.renderPluginValues[0].schemaVersion = 9;
            if (kind == 3) invalid.renderer.renderPluginValues[0].values.push_back(number("gain", 0.7f));
            if (kind == 4) invalid.renderer.renderPluginValues[0].values[0].value.number = 9;
            require(!saveSceneDocument(independentPath, invalid, error, catalog)
                && bytes(independentPath) == independentBytes, "Invalid store truncated valid Scene");
        }
        require(!stableSchema->apply(independent, {number("gain", 1), number("unknown", 1)}, history, error)
            && !history && stableSchema->capture(independent)[1].value.number == 1.5f, "Unbound partial commit escaped");
        auto defaults = storedSchema.metadata; defaults.resetToDefaults();
        std::vector<ModuleParameterOverride> reset;
        for (const auto& d : defaults.descriptors()) reset.push_back({d.id, *defaults.value(d.id)});
        require(stableSchema->apply(independent, reset, history, error)
            && independent.renderPluginValues.size() == 1
            && independent.renderPluginValues[0].pluginId == "test.other", "Reset defaults retained stale overrides or erased another plugin");
        auto mixed = storedPluginSchema("test.mixed");
        mixed.bindings.push_back({"gain", [](const RendererSettings& v) { ModuleParameterValue n; n.type=ModuleParameterType::Float; n.number=v.exposure; return n; },
            [](RendererSettings& v, const ModuleParameterValue& n) { v.exposure=n.number; }, false});
        mixed.validate();
        auto mixedValues = mixed.capture(independent); mixedValues[0].value.boolean = false;
        mixedValues[1].value.number = 1.7f;
        require(mixed.apply(independent, mixedValues, history, error) && independent.exposure == 1.7f,
            "Mixed bound/unbound schema rejected");
        independent.exposure = 1.2f;
        require(mixed.capture(independent)[1].value.number == 1.2f, "Stored values shadowed legacy binding");
        mixed.bindings[0].write = [](RendererSettings&, const ModuleParameterValue&) { throw std::runtime_error("fixture"); };
        mixedValues[0].value.boolean = true;
        require(!mixed.apply(independent, mixedValues, history, error)
            && !mixed.capture(independent)[0].value.boolean && independent.exposure == 1.2f, "Mixed callback failure partly stored values");
        auto resourceSchema=storedPluginSchema("test.assets"); resourceSchema.version=2;
        resourceSchema.metadata.registerAsset("texture","Texture",".rgb;.PNG");
        resourceSchema.migrate=[](int version,std::vector<ModuleParameterOverride>& values,std::string&) {
            if(version!=1) return false;
            for(auto& value:values) {
                if(value.id=="strength") value.id="gain";
                if(value.id=="preset" && value.value.text=="legacy") value.value.text="normal";
            }
            return true;
        };
        catalog.add(resourceSchema);
        const auto* resources=catalog.find("test.assets");
        RendererSettings assetSettings;
        ModuleParameterOverride asset; asset.id="texture"; asset.value.type=ModuleParameterType::Asset;
        const auto resourcePath=directory/"resource with spaces.rgb";
        { std::ofstream file(resourcePath); file<<"0.2 0.3 0.4"; }
        asset.value.text=resourcePath.generic_u8string();
        require(resources->apply(assetSettings,{asset},history,error),"Asset path rejected");
        SceneDocument assetScene; assetScene.renderer=assetSettings;
        const auto assetScenePath=directory/"asset.myscene";
        require(saveSceneDocument(assetScenePath,assetScene,error,catalog),"Asset save failed");
        require(bytes(assetScenePath).find("resource with spaces.rgb")!=std::string::npos,"Asset reference not saved");
        require(loadSceneDocument(assetScenePath,assetScene,error,catalog),"Asset load failed");
        require(resources->capture(assetScene.renderer).back().value.text==std::filesystem::absolute(resourcePath).generic_u8string(),
            "Relative Asset did not resolve at Scene directory");
        const auto originalDirectory=std::filesystem::current_path();
        std::filesystem::current_path(directory);
        const bool relativeSaved=saveSceneDocument("relative-assets.myscene",assetScene,error,catalog);
        std::filesystem::current_path(originalDirectory);
        require(relativeSaved && bytes(directory/"relative-assets.myscene").find("\"value\": \"resource with spaces.rgb\"")!=std::string::npos,
            "Relative Scene filename produced an absolute Asset reference");
        for(const auto& bad : std::vector<std::string>{"invalid.exe","https://example.org/image.rgb",std::string("a\0.rgb",6)}) {
            asset.value.text=bad;
            require(!resources->apply(assetSettings,{asset},history,error) && !history,"Invalid Asset accepted");
        }
        auto old=number("strength",1.6f);
        ModuleParameterOverride oldEnum; oldEnum.id="preset"; oldEnum.value.type=ModuleParameterType::Enum; oldEnum.value.text="legacy";
        require(resources->importValues(assetSettings,1,{old,oldEnum},history,error)
            && resources->capture(assetSettings)[1].value.number==1.6f,"Explicit schema migration failed");
        require(!resources->importValues(assetSettings,3,{old},history,error)
            && resources->capture(assetSettings)[1].value.number==1.6f,"Future schema mutated state");
        auto badMigration=resourceSchema;
        badMigration.migrate=[](int,std::vector<ModuleParameterOverride>& values,std::string&) { values={number("gain",9)}; return true; };
        require(!badMigration.importValues(assetSettings,1,{old},history,error)
            && resources->capture(assetSettings)[1].value.number==1.6f,"Invalid migrated values partly published");
        { std::ofstream file(directory/"migration.myscene"); file<<prefix
            <<"{\"version\":1,\"entries\":[{\"id\":\"test.assets\",\"schemaVersion\":1,\"values\":[{\"id\":\"strength\",\"type\":2,\"value\":1.3},{\"id\":\"preset\",\"type\":4,\"value\":\"legacy\"}]}]}}}"; }
        require(loadSceneDocument(directory/"migration.myscene",assetScene,error,catalog)
            && resources->capture(assetScene.renderer)[1].value.number==1.3f,"Scene did not migrate old schema");
        std::cout << "Plugin parameters: shared values, history, edit/load transactions and roundtrip PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
