#include "module/BuiltinModules.h"
#include "module/ModuleRegistry.h"
#include "module/ModuleRuntime.h"
#include "module/ParameterRegistry.h"
#include "module/SimulationCache.h"
#include "module/RuntimeScene.h"
#include "render/Camera.h"
#include "render/Renderer.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void requireClose(float actual, float expected, const char* message) {
    if (std::abs(actual - expected) > 1.0e-4f) {
        throw std::runtime_error(
            std::string(message) + ": expected " + std::to_string(expected)
            + ", got " + std::to_string(actual)
        );
    }
}

// A module that fails on purpose, so failure isolation is covered without shipping a
// broken module in the product build. `logInsteadOfFailing` selects "fixedUpdate returns
// false" versus "fixedUpdate succeeds but logs an Error".
class FailingModule final : public ISceneModule {
public:
    explicit FailingModule(bool logInsteadOfFailing)
        : logInsteadOfFailing_(logInsteadOfFailing) {}

    ModuleManifest manifest() const override {
        ModuleManifest result;
        result.id = "test.failing";
        result.displayName = "Failing (test only)";
        result.kind = ModuleKind::Simulation;
        result.cmakeTarget = "MyRendererModuleTests";
        result.sourceRoot = "tests";
        result.apiVersion = moduleApiVersion;
        result.buildId = moduleBuildId();
        return result;
    }
    void registerParameters(ParameterRegistry& parameters) override {
        parameters.registerBool("enabled", "Enabled", true, "test parameter");
    }
    bool initialize(const SceneContext& context, std::string& error) override {
        (void)context;
        (void)error;
        return true;
    }
    void reset() override {}
    bool fixedUpdate(SceneContext& context, double fixedDeltaSeconds, std::string& error) override {
        (void)fixedDeltaSeconds;
        if (logInsteadOfFailing_) {
            context.log(ModuleLogSeverity::Error, "intentional module error");
            return true;
        }
        error = "intentional module failure";
        return false;
    }

private:
    bool logInsteadOfFailing_{false};
};

// Two animated entities plus one that references no model. The second entity is a
// child of the first, so the runtime copy must preserve the hierarchy.
Scene buildEditScene(SceneEntityId& first, SceneEntityId& second, SceneEntityId& stage) {
    Scene scene;
    first = scene.createEntity("Orbit A", nullptr, "assets/models/cube.obj");
    second = scene.createEntity("Orbit B", nullptr, "assets/models/sphere.obj");
    stage = scene.createEntity("Stage");
    scene.find(first)->transform.translation = {1.0f, 0.5f, 0.0f};
    scene.find(first)->transform.rotationDegrees = {0.0f, 10.0f, 0.0f};
    scene.find(first)->tint = {0.8f, 0.8f, 0.9f};
    scene.find(second)->transform.translation = {-2.0f, 0.25f, 0.0f};
    scene.setParent(second, first);
    scene.updateWorldTransforms();
    return scene;
}

void testParameterRegistry() {
    ParameterRegistry parameters;
    parameters.registerBool("enabled", "Enabled", true, "toggle");
    parameters.registerInt("count", "Count", 4, 1, 8, "how many");
    parameters.registerFloat("speed", "Speed", 2.5f, -10.0f, 10.0f, "per frame");
    parameters.registerColor("tint", "Tint", glm::vec3(1.0f, 0.5f, 0.25f), "colour");
    parameters.registerEnum("axis", "Axis", {"X", "Y", "Z"}, 1, "rotation axis");
    parameters.registerAsset("clip", "Clip", ".myscene;.gltf", "optional asset");
    require(parameters.size() == 6U, "every registered parameter must be stored");
    require(parameters.descriptors()[1].id == "count", "descriptors keep registration order");
    require(parameters.descriptors()[1].type == ModuleParameterType::Int,
            "descriptors must keep their declared type");
    require(parameters.descriptors()[1].minimum == 1.0 && parameters.descriptors()[1].maximum == 8.0,
            "int range must be recorded");
    require(parameters.descriptors()[4].enumLabels.size() == 3U, "enum labels must be recorded");
    require(parameters.descriptors()[5].assetExtensionFilter == ".myscene;.gltf",
            "asset extension filter must be recorded");

    require(parameters.isDefault("speed"), "a fresh registry is at its defaults");
    require(parameters.overrides().empty(), "defaults produce no persisted override");
    require(parameters.boolValue("enabled", false), "bool default must be readable");
    require(parameters.intValue("count", 0) == 4, "int default must be readable");
    requireClose(parameters.floatValue("speed", 0.0f), 2.5f, "float default must be readable");
    require(parameters.enumLabel("axis") == "Y", "enum default label must be readable");

    std::string error;
    require(!parameters.setFloat("enabled", 1.0f, error), "a type mismatch must fail");
    require(!error.empty(), "a rejected write must explain itself");
    require(!parameters.setFloat("missing", 1.0f, error), "an unknown id must fail");
    require(error.find("missing") != std::string::npos, "the error must name the parameter");

    require(parameters.setFloat("speed", 99.0f, error), "an out-of-range number is clamped, not rejected");
    requireClose(parameters.floatValue("speed", 0.0f), 10.0f, "clamping must use the declared maximum");
    require(!parameters.isDefault("speed"), "a clamped value is no longer the default");
    require(!parameters.setFloat("speed", std::nanf(""), error), "NaN must be rejected, not stored");
    require(error.find("finite") != std::string::npos, "NaN must be reported as non-finite");
    requireClose(parameters.floatValue("speed", 0.0f), 10.0f, "a rejected write must not change the value");

    require(parameters.setColor("tint", glm::vec3(-1.0f, 0.5f, 3.0f), error),
            "colour channels are clamped per channel");
    requireClose(parameters.colorValue("tint", glm::vec3(0.0f)).x, 0.0f, "colour clamps to the minimum");
    requireClose(parameters.colorValue("tint", glm::vec3(0.0f)).z, 1.0f, "colour clamps to the maximum");

    require(parameters.setEnumIndex("axis", 9, error), "an unknown enum index is clamped");
    require(parameters.enumLabel("axis") == "Z", "a clamped enum index must update its label");
    require(!parameters.setEnumLabel("axis", "W", error), "an unknown enum label must fail");

    require(parameters.setAsset("clip", "assets/scenes/fixtures/01_multi_model_hierarchy.myscene", error),
            "an asset matching the filter must be accepted");
    require(!parameters.setAsset("clip", "assets/models/cube.obj", error),
            "an asset outside the filter must be rejected");
    require(parameters.assetValue("clip") == "assets/scenes/fixtures/01_multi_model_hierarchy.myscene",
            "a rejected asset must leave the previous value in place");
    require(parameters.setAsset("clip", "", error), "an unset asset is allowed");
    require(parameters.assetValue("clip").empty(), "an empty asset value clears the parameter");

    const std::vector<ModuleParameterOverride> overrides = parameters.overrides();
    require(overrides.size() == 3U, "only changed parameters become overrides");
    require(overrides[0].id == "speed" && overrides[1].id == "tint" && overrides[2].id == "axis",
            "overrides keep registration order");

    parameters.resetToDefaults();
    require(parameters.overrides().empty(), "reset must clear every override");
    require(parameters.setFloat("speed", 7.5f, error), "a modified registry accepts writes");
    std::vector<ModuleParameterOverride> rejected{
        ModuleParameterOverride{"speed", ModuleParameterValue{ModuleParameterType::Float, false, 0, 5.0f, glm::vec3(0.0f), {}}},
        ModuleParameterOverride{"unknown", ModuleParameterValue{ModuleParameterType::Float, false, 0, 5.0f, glm::vec3(0.0f), {}}}
    };
    require(!parameters.applyOverrides(rejected, error), "an unknown override id must fail the load");
    requireClose(parameters.floatValue("speed", 0.0f), 7.5f,
                 "a rejected override set must not be partially applied");
    std::vector<ModuleParameterOverride> accepted{
        ModuleParameterOverride{"speed", ModuleParameterValue{ModuleParameterType::Float, false, 0, 5.0f, glm::vec3(0.0f), {}}},
        ModuleParameterOverride{"count", ModuleParameterValue{ModuleParameterType::Int, false, 6, 0.0f, glm::vec3(0.0f), {}}}
    };
    require(parameters.applyOverrides(accepted, error), "a valid override set must apply");
    requireClose(parameters.floatValue("speed", 0.0f), 5.0f, "an override must reach the value");
    require(parameters.intValue("count", 0) == 6, "every override in the set must apply");
}

void testRuntimeScene(SceneEntityId first, SceneEntityId second, SceneEntityId stage,
                      const Scene& editScene) {
    RuntimeScene runtime;
    require(runtime.generation() == 0U, "a fresh runtime scene starts at generation 0");
    const std::uint64_t firstGeneration = runtime.resetFrom(editScene);
    require(firstGeneration == 1U, "resetFrom must advance the generation");
    require(!runtime.dirty(), "a fresh copy is not dirty");
    require(runtime.scene().size() == editScene.size(), "the copy must contain every entity");
    require(runtime.scene().find(second)->parent == first,
            "the copy must preserve the parent-child hierarchy");
    requireClose(runtime.scene().find(first)->transform.translation.x, 1.0f,
                 "the copy must preserve transforms");
    require(runtime.contentHash() == sceneContentHash(editScene),
            "an unmodified copy must hash like its input");

    // Editing the copy must not reach the edit scene.
    runtime.scene().find(first)->transform.translation.x = 42.0f;
    requireClose(editScene.find(first)->transform.translation.x, 1.0f,
                 "the runtime scene must never write through to the edit scene");
    require(runtime.contentHash() != sceneContentHash(editScene),
            "a changed runtime scene must hash differently");

    runtime.markDirty();
    require(runtime.dirty(), "markDirty must be observable");
    const std::uint64_t secondGeneration = runtime.resetFrom(editScene);
    require(secondGeneration == firstGeneration + 1U, "a second reset must advance the generation");
    require(!runtime.dirty(), "resetFrom must clear the dirty flag");
    requireClose(runtime.scene().find(first)->transform.translation.x, 1.0f,
                 "resetFrom must discard previous runtime edits");

    // The same content built in a different order must hash the same.
    Scene reordered;
    reordered.createEntityWithId(runtime.scene().find(second)->id, "Orbit B", nullptr,
                                 "assets/models/sphere.obj");
    reordered.createEntityWithId(runtime.scene().find(first)->id, "Orbit A", nullptr,
                                 "assets/models/cube.obj");
    reordered.createEntityWithId(stage, "Stage", nullptr, "");
    reordered.find(first)->transform.translation = {1.0f, 0.5f, 0.0f};
    reordered.find(first)->transform.rotationDegrees = {0.0f, 10.0f, 0.0f};
    reordered.find(first)->tint = {0.8f, 0.8f, 0.9f};
    reordered.find(second)->transform.translation = {-2.0f, 0.25f, 0.0f};
    reordered.find(second)->parent = first;
    require(sceneContentHash(reordered) == runtime.contentHash(),
            "the content hash must not depend on insertion order");
    reordered.find(first)->transform.translation.x = 1.5f;
    require(sceneContentHash(reordered) != runtime.contentHash(),
            "the content hash must follow a transform change");
}

void testModuleRegistry() {
    ModuleRegistry registry = createBuiltinModuleRegistry();
    require(registry.size() >= 1U, "the builtin registry must not be empty");
    require(registry.contains(BuiltinModules::turntableId),
            "the turntable module must be registered by its stable id");
    const ModuleManifest* manifest = registry.find(BuiltinModules::turntableId);
    require(manifest != nullptr, "a registered module must be findable");
    require(manifest->apiVersion == moduleApiVersion, "the manifest must report the module API version");
    require(manifest->cmakeTarget == "MyRendererModules", "the manifest must name its CMake target");
    require(!manifest->buildId.empty(), "the manifest must carry a build id");
    require(std::string(moduleKindName(manifest->kind)) == "Scene", "the turntable is a scene module");

    const std::vector<ModuleManifest> manifests = registry.manifests();
    require(manifests.size() == registry.size(), "manifests must cover the whole registry");
    for (std::size_t index = 1U; index < manifests.size(); ++index) {
        require(manifests[index - 1U].id < manifests[index].id,
                "manifests must be sorted by id for deterministic panels and reports");
    }

    std::string error;
    require(registry.create("missing.module", error) == nullptr, "an unknown id must not create");
    require(error.find("missing.module") != std::string::npos, "the error must name the module");

    ModuleRegistry duplicate;
    require(!duplicate.add(manifests.front(), ModuleRegistry::Factory{}, error),
            "a module without a factory must be rejected");
    require(error.find("factory") != std::string::npos, "the missing factory must be explained");
    const ModuleRegistry::Factory nullFactory = []() -> std::unique_ptr<ISceneModule> {
        return nullptr;
    };
    require(duplicate.add(manifests.front(), nullFactory, error),
            "the first registration of an id must succeed");
    require(!duplicate.add(manifests.front(), nullFactory, error), "duplicate ids must be rejected");
    require(error.find("already registered") != std::string::npos,
            "a duplicate id must be explained");
    require(duplicate.create(manifests.front().id, error) == nullptr,
            "a factory that returns nothing must be reported by create");
    require(error.find("no instance") != std::string::npos,
            "an empty factory result must be explained");
    ModuleManifest badVersion = manifests.front();
    badVersion.id = "test.bad-version";
    badVersion.apiVersion = moduleApiVersion + 1;
    require(!duplicate.add(badVersion, nullFactory, error),
            "a module built against another API version must be rejected");
    require(error.find("API version") != std::string::npos, "the API mismatch must be explained");
}

void testTurntableModule(SceneEntityId first, SceneEntityId second, SceneEntityId stage,
                         const Scene& editScene) {
    ModuleRegistry registry = createBuiltinModuleRegistry();
    std::string error;
    std::unique_ptr<ISceneModule> module = registry.create(BuiltinModules::turntableId, error);
    require(module != nullptr, "the turntable module must be creatable");

    ParameterRegistry parameters;
    module->registerParameters(parameters);
    require(parameters.size() == 8U, "the turntable must declare its full parameter set");
    require(parameters.contains("degreesPerFrame") && parameters.contains("axis"),
            "the turntable must expose its animation controls");
    require(parameters.isDefault("degreesPerFrame"), "a fresh module starts on its defaults");

    RuntimeScene runtime;
    runtime.resetFrom(editScene);
    Timeline timeline;
    timeline.setFrameRange(0, 23);
    SceneContext context(runtime, timeline, parameters, 20260919U);
    require(context.seed() == 20260919U, "the context must expose the run seed");
    require(module->initialize(context, error), "initialize must succeed");

    require(module->fixedUpdate(context, timeline.fixedDeltaSeconds(), error),
            "the first update must succeed");
    require(!runtime.dirty(), "frame 0 must reproduce the baseline and stay clean");
    requireClose(runtime.scene().find(first)->transform.rotationDegrees.y, 10.0f,
                 "frame 0 must keep the baseline rotation");

    // 15 degrees per frame and 24 frames per second: frame 6 is a quarter turn.
    timeline.setFrame(6);
    require(module->fixedUpdate(context, timeline.fixedDeltaSeconds(), error), "update must succeed");
    require(runtime.dirty(), "an animated frame must mark the runtime scene dirty");
    requireClose(runtime.scene().find(first)->transform.rotationDegrees.y, 100.0f,
                 "frame 6 must add 90 degrees to the baseline rotation");
    requireClose(runtime.scene().find(first)->transform.rotationDegrees.x, 0.0f,
                 "an untouched axis must stay on its baseline");
    requireClose(runtime.scene().find(stage)->transform.rotationDegrees.y, 0.0f,
                 "an entity without a model must not be animated");

    // Idempotent for one frame, and a scrub is a pure function of the frame index.
    const float frameSixRotation = runtime.scene().find(first)->transform.rotationDegrees.y;
    require(module->fixedUpdate(context, timeline.fixedDeltaSeconds(), error), "a repeat must succeed");
    requireClose(runtime.scene().find(first)->transform.rotationDegrees.y, frameSixRotation,
                 "repeating a frame must not accumulate");
    timeline.setFrame(10);
    require(module->fixedUpdate(context, timeline.fixedDeltaSeconds(), error), "update must succeed");
    const float frameTenRotation = runtime.scene().find(first)->transform.rotationDegrees.y;
    timeline.setFrame(3);
    require(module->fixedUpdate(context, timeline.fixedDeltaSeconds(), error), "update must succeed");
    requireClose(runtime.scene().find(first)->transform.rotationDegrees.y, 55.0f,
                 "scrubbing backwards must compute the frame value, not a delta");
    timeline.setFrame(10);
    require(module->fixedUpdate(context, timeline.fixedDeltaSeconds(), error), "update must succeed");
    requireClose(runtime.scene().find(first)->transform.rotationDegrees.y, frameTenRotation,
                 "returning to a frame must reproduce it exactly");

    // Carousel and phase are absolute, so a single evaluation is enough to predict them.
    require(parameters.setFloat("degreesPerFrame", 90.0f, error), "parameter write must succeed");
    require(parameters.setFloat("carouselRadius", 2.0f, error), "parameter write must succeed");
    require(parameters.setFloat("phaseDegrees", 0.0f, error), "parameter write must succeed");
    timeline.setFrame(1);
    require(module->fixedUpdate(context, timeline.fixedDeltaSeconds(), error), "update must succeed");
    requireClose(runtime.scene().find(first)->transform.translation.x, 1.0f,
                 "a quarter-turn carousel must not move the x axis for a Y rotation");
    requireClose(runtime.scene().find(first)->transform.translation.z, 2.0f,
                 "the carousel radius must offset along the rotation plane");
    require(parameters.setEnumIndex("axis", 0, error), "the rotation axis must be selectable");
    require(module->fixedUpdate(context, timeline.fixedDeltaSeconds(), error), "update must succeed");
    requireClose(runtime.scene().find(first)->transform.rotationDegrees.x, 90.0f,
                 "selecting the X axis must rotate about X");
    requireClose(runtime.scene().find(first)->transform.rotationDegrees.y, 10.0f,
                 "switching axis must restore the other axes to the baseline");

    require(parameters.setColor("tintColor", glm::vec3(0.0f, 1.0f, 0.0f), error),
            "the tint colour must be settable");
    require(parameters.setFloat("tintStrength", 1.0f, error), "the tint strength must be settable");
    require(module->fixedUpdate(context, timeline.fixedDeltaSeconds(), error), "update must succeed");
    requireClose(runtime.scene().find(first)->tint.y, 1.0f, "a full tint strength must replace the tint");

    // Disabling the module restores the baseline exactly.
    require(parameters.setBool("enabled", false, error), "the module must be switchable");
    require(module->fixedUpdate(context, timeline.fixedDeltaSeconds(), error), "update must succeed");
    requireClose(runtime.scene().find(first)->transform.rotationDegrees.x, 0.0f,
                 "a disabled module must restore the baseline rotation");
    requireClose(runtime.scene().find(first)->tint.y, 0.8f,
                 "a disabled module must restore the baseline tint");

    // Deterministic state, and reset returns the module to its capture step.
    const std::string state = module->serializeState();
    require(!state.empty(), "the module must expose deterministic state");
    std::unique_ptr<ISceneModule> repeat = registry.create(BuiltinModules::turntableId, error);
    ParameterRegistry repeatParameters;
    repeat->registerParameters(repeatParameters);
    RuntimeScene repeatRuntime;
    repeatRuntime.resetFrom(editScene);
    SceneContext repeatContext(repeatRuntime, timeline, repeatParameters, 20260919U);
    require(repeat->initialize(repeatContext, error), "a second instance must initialize");
    require(repeat->serializeState() == state,
            "two instances with the same baseline must serialize the same state");
    module->reset();
    require(module->fixedUpdate(context, timeline.fixedDeltaSeconds(), error),
            "a reset module must be usable again");
    require(runtime.scene().find(second)->parent == first,
            "the runtime hierarchy must survive a module step");
    require(module->bake(context, error), "bake must succeed");
    require(!context.hasErrors(), "a successful run must not log errors");

    bool cancelled = true;
    context.setCancellationCheck([&cancelled]() { return cancelled; });
    require(context.cancellationRequested(), "the host cancellation check must be observable");
    context.setCancellationCheck([]() { return false; });
    require(!context.cancellationRequested(), "cancellation must be clearable");
}

void testSimulationCache(SceneEntityId first, SceneEntityId second, SceneEntityId stage,
                         const Scene& editScene) {
    (void)first;
    (void)second;
    (void)stage;
    ModuleRegistry registry = createBuiltinModuleRegistry();
    std::string error;
    ModuleRuntime runtime(registry);
    require(runtime.configure(BuiltinModules::turntableId, {}, 7U, error), error.c_str());
    SimulationCache cache;
    require(runtime.bakeSimulation(editScene, 0, 5, 24, cache, error), error.c_str());
    require(cache.frames.size() == 6U, "a bake must record one frame per step");
    require(cache.key.moduleId == BuiltinModules::turntableId, "the cache key must record the module");
    require(cache.key.seed == 7U, "the cache key must record the seed");
    require(cache.key.framesPerSecond == 24, "the cache key must record the fixed step");
    require(cache.key.startFrame == 0 && cache.key.endFrame == 5, "the cache key must record the range");
    require(cache.key.sceneContentHash == sceneContentHash(editScene),
            "the cache key must record the authored scene hash");
    require(!cache.key.buildId.empty(), "the cache key must record the build id");
    require(cache.covers(0) && cache.covers(5) && !cache.covers(6),
            "cache coverage must follow the baked range");

    ModuleRuntime repeat(registry);
    require(repeat.configure(BuiltinModules::turntableId, {}, 7U, error), error.c_str());
    SimulationCache repeatCache;
    require(repeat.bakeSimulation(editScene, 0, 5, 24, repeatCache, error), error.c_str());
    require(repeatCache.frames.size() == cache.frames.size(),
            "a repeated bake must record the same frame count");
    for (std::size_t index = 0; index < cache.frames.size(); ++index) {
        require(cache.frames[index].contentHash == repeatCache.frames[index].contentHash,
                "a repeated bake must produce the same per-frame content hash");
        require(cache.frames[index].moduleState == repeatCache.frames[index].moduleState,
                "a repeated bake must produce the same module state");
    }

    const std::filesystem::path cachePath = std::filesystem::temp_directory_path()
        / "MyRendererModuleCacheTests" / "simulation-cache.json";
    std::error_code cleanup;
    std::filesystem::remove_all(cachePath.parent_path(), cleanup);
    require(saveSimulationCache(cachePath, cache, error), error.c_str());
    SimulationCache loaded;
    require(loadSimulationCache(cachePath, loaded, error), error.c_str());
    require(loaded.key == cache.key, "the cache key must survive a round trip");
    require(loaded.frames.size() == cache.frames.size(), "every frame must survive a round trip");
    const SimulationCacheFrame* original = cache.find(3);
    const SimulationCacheFrame* restored = loaded.find(3);
    require(original != nullptr && restored != nullptr, "frame 3 must be present after a round trip");
    require(original->contentHash == restored->contentHash,
            "a cached frame content hash must survive a round trip");
    require(original->moduleState == restored->moduleState,
            "cached module state must survive a round trip");
    require(original->entities.size() == restored->entities.size(),
            "cached entities must survive a round trip");
    requireClose(
        restored->entities.front().transform.translation.x,
        original->entities.front().transform.translation.x,
        "a cached transform must survive a round trip"
    );

    std::string message;
    require(classifySimulationCache(loaded, loaded.key, message) == SimulationCacheStatus::Hit,
            "a matching key must be a cache hit");
    // A parameter edit changes the simulation while scene, seed and build stay the same.
    // The recorded content hashes cannot detect that (a cached frame always hashes to the
    // value it recorded), so the parameter fingerprint has to be part of the key.
    require(runtime.parameters().setFloat("degreesPerFrame", 45.0f, error),
            "a parameter write must succeed");
    SimulationCacheKey changedParameters = runtime.cacheKey();
    require(changedParameters.parameterHash != loaded.key.parameterHash,
            "a parameter change must change the key fingerprint");
    require(classifySimulationCache(loaded, changedParameters, message)
                == SimulationCacheStatus::Stale,
            "a parameter change must invalidate the cache");
    require(message.find("parameters") != std::string::npos,
            "the stale message must name the changed parameters");
    require(runtime.parameters().setFloat("degreesPerFrame", 15.0f, error),
            "restoring a parameter must succeed");
    require(runtime.cacheKey().parameterHash == loaded.key.parameterHash,
            "restoring the parameter must restore the fingerprint");
    SimulationCacheKey changedSeed = loaded.key;
    changedSeed.seed = 99U;
    require(classifySimulationCache(loaded, changedSeed, message) == SimulationCacheStatus::Stale,
            "a different seed must be stale");
    require(message.find("seed") != std::string::npos,
            "the stale message must name the changed input");
    SimulationCacheKey changedScene = loaded.key;
    changedScene.sceneContentHash += 1U;
    require(classifySimulationCache(loaded, changedScene, message) == SimulationCacheStatus::Stale,
            "a different scene hash must be stale");
    SimulationCacheKey changedRange = loaded.key;
    changedRange.endFrame = 9;
    require(classifySimulationCache(loaded, changedRange, message) == SimulationCacheStatus::Stale,
            "a different frame range must be stale");

    ModuleRuntime replay(registry);
    require(replay.configure(BuiltinModules::turntableId, {}, 7U, error), error.c_str());
    require(replay.reset(editScene, 0, 5, 24, error), error.c_str());
    require(replay.applyCachedFrame(*cache.find(4), error), error.c_str());
    require(replay.report().contentHash == cache.find(4)->contentHash,
            "a replayed frame must reproduce its recorded content hash");
    require(replay.report().lastFrame == 4, "a replayed frame must report its frame");
    SimulationCacheFrame tampered = *cache.find(4);
    require(!tampered.entities.empty(), "a cached frame must carry entities");
    tampered.entities.front().transform.translation.x += 5.0f;
    require(!replay.applyCachedFrame(tampered, error),
            "a tampered cached frame must be refused");
    require(error.find("content hash") != std::string::npos,
            "the refusal must explain the content hash mismatch");
    std::filesystem::remove_all(cachePath.parent_path(), cleanup);
}

void testModuleCancellation(const Scene& editScene) {
    ModuleRegistry registry = createBuiltinModuleRegistry();
    std::string error;
    ModuleRuntime runtime(registry);
    require(runtime.configure(BuiltinModules::turntableId, {}, 1U, error), error.c_str());
    bool cancelled = false;
    runtime.setCancellationCheck([&cancelled]() { return cancelled; });
    require(runtime.reset(editScene, 0, 5, 24, error), error.c_str());
    require(runtime.runToFrame(2, error), error.c_str());
    require(runtime.report().status == ModuleRunStatus::Ready,
            "a completed run must report Ready");
    cancelled = true;
    require(!runtime.runToFrame(4, error), "a cancelled run must stop");
    require(runtime.report().status == ModuleRunStatus::Cancelled,
            "cancellation must be reported as Cancelled, not as a failure");
    require(error.find("cancel") != std::string::npos,
            "the cancellation must be explained");
    require(runtime.report().lastFrame == 2,
            "cancellation must stop at a frame boundary");
}

void testModuleFailureIsolation(SceneEntityId first, const Scene& editScene) {
    const float authoredRotation = editScene.find(first)->transform.rotationDegrees.y;
    for (bool logInsteadOfFailing : {false, true}) {
        ModuleRegistry single;
        std::string error;
        require(single.add(
                    FailingModule{logInsteadOfFailing}.manifest(),
                    [logInsteadOfFailing]() -> std::unique_ptr<ISceneModule> {
                        return std::make_unique<FailingModule>(logInsteadOfFailing);
                    },
                    error
                ),
                error.c_str());
        ModuleRuntime runtime(single);
        require(runtime.configure("test.failing", {}, 1U, error), error.c_str());
        require(!runtime.reset(editScene, 0, 3, 24, error),
                logInsteadOfFailing
                    ? "a module that logs an Error must fail the run"
                    : "a failing fixedUpdate must fail the run");
        require(runtime.report().status == ModuleRunStatus::Failed,
                "a failed run must report Failed");
        require(!runtime.report().error.empty(), "a failed run must report a reason");
        require(!runtime.runToFrame(2, error),
                "a failed runtime must refuse to keep stepping");
        // Failure isolation: the authored scene is untouched, so the editor keeps working.
        requireClose(editScene.find(first)->transform.rotationDegrees.y, authoredRotation,
                     "a failing module must leave the authored scene untouched");
    }
}

void testCoastalSequence(const Scene& editScene) {
    ModuleRegistry registry = createBuiltinModuleRegistry();
    require(registry.contains(BuiltinModules::coastalSequenceId),
            "the coastal sequence must be registered");
    std::string error;
    ModuleRuntime runtime(registry);
    require(runtime.configure(BuiltinModules::coastalSequenceId, {}, 7U, error), error.c_str());
    require(runtime.reset(editScene, 0, 24, 24, error), error.c_str());
    CameraOrbitState authoredCamera;
    RendererSettings authoredRenderer;
    authoredRenderer.water.enabled = false;
    authoredRenderer.atmosphere.enabled = false;
    CameraOrbitState startCamera;
    RendererSettings startRenderer;
    runtime.applyPresentation(authoredCamera, authoredRenderer, startCamera, startRenderer);
    require(startRenderer.water.enabled && startRenderer.atmosphere.enabled,
            "the sequence must enable water and the common solar sky");
    requireClose(startRenderer.atmosphere.sunElevationDegrees, 48.0f, "noon sun");
    require(startRenderer.atmosphere.nightSkyEnabled,
            "the sequence must enable optional moon and stars");
    requireClose(startRenderer.water.amplitude, 0.08f, "calm sea");
    requireClose(startRenderer.water.timeSeconds, 0.0f, "first wave time");
    require(runtime.runToFrame(24, error), error.c_str());
    CameraOrbitState endCamera;
    RendererSettings endRenderer;
    runtime.applyPresentation(authoredCamera, authoredRenderer, endCamera, endRenderer);
    requireClose(endRenderer.atmosphere.sunElevationDegrees, -8.0f, "night sun");
    requireClose(endRenderer.atmosphere.moonIntensity, 1.0f, "night moon intensity");
    require(endRenderer.atmosphere.skyIntensity < startRenderer.atmosphere.skyIntensity,
            "night sky energy must fall below daylight");
    requireClose(endRenderer.atmosphere.aerialPerspectiveStrength, 1.1f, "sunset fog");
    requireClose(endRenderer.water.amplitude, 0.34f, "rough sea");
    requireClose(endRenderer.water.timeSeconds, 1.0f, "last wave time");
    requireClose(endCamera.yawDegrees, 4.0f, "camera trajectory");
    requireClose(authoredRenderer.water.amplitude, WaterSettings{}.amplitude,
                 "preview must preserve authored settings");
    require(runtime.reset(editScene, 0, 24, 24, error), error.c_str());
    require(runtime.runToFrame(24, error), error.c_str());
    CameraOrbitState repeatedCamera;
    RendererSettings repeatedRenderer;
    runtime.applyPresentation(authoredCamera, authoredRenderer, repeatedCamera, repeatedRenderer);
    requireClose(repeatedRenderer.water.amplitude, endRenderer.water.amplitude,
                 "reset and scrub must reproduce the same sea");
    requireClose(repeatedRenderer.atmosphere.sunAzimuthDegrees,
                 endRenderer.atmosphere.sunAzimuthDegrees, "reset and scrub must reproduce the sun");
}

} // namespace

int main() {
    try {
        testParameterRegistry();
        testModuleRegistry();
        SceneEntityId first = invalidSceneEntityId;
        SceneEntityId second = invalidSceneEntityId;
        SceneEntityId stage = invalidSceneEntityId;
        const Scene editScene = buildEditScene(first, second, stage);
        testRuntimeScene(first, second, stage, editScene);
        testTurntableModule(first, second, stage, editScene);
        testSimulationCache(first, second, stage, editScene);
        testModuleCancellation(editScene);
        testModuleFailureIsolation(first, editScene);
        testCoastalSequence(editScene);
        std::cout << "Module runtime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Module runtime tests failed: " << error.what() << '\n';
        return 1;
    }
}
