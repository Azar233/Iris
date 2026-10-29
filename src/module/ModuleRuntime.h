#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "module/ModuleRegistry.h"
#include "module/SceneModule.h"
#include "module/SimulationCache.h"

// Owns one module instance together with its parameter registry, its discardable
// runtime scene and the deterministic timeline.
//
// The GUI preview and the headless batch runtime both drive modules through this
// one class, so they cannot disagree about seed, fixed step, frame range or how a
// parameter value is interpreted. Frames are always reached by stepping forward
// from the start frame, which keeps an accumulating simulation reproducible; the
// caller decides how often to rebuild (the batch rebuilds per rendered frame, which
// is O(n^2) in frames and deliberately so: every frame is then a pure function of
// the job inputs).

enum class ModuleRunStatus {
    Idle = 0,
    // The runtime scene holds a valid module state for `report().lastFrame`.
    Ready,
    Cancelled,
    Failed
};

const char* moduleRunStatusName(ModuleRunStatus status);

struct ModuleRunReport {
    ModuleRunStatus status{ModuleRunStatus::Idle};
    std::string moduleId;
    std::string moduleDisplayName;
    int apiVersion{moduleApiVersion};
    std::string buildId;
    std::uint32_t seed{0U};
    int startFrame{0};
    int endFrame{0};
    int framesPerSecond{24};
    // Content hash of the runtime scene right after it was copied from the edit
    // scene: the simulation cache key input.
    std::uint64_t inputContentHash{0U};
    int lastFrame{0};
    std::uint64_t contentHash{0U};
    std::string moduleState;
    std::string error;
    ModuleSimulationStatistics simulation;
    ModuleSimulationConfiguration simulationConfiguration;
};

// Applies parameter values coming from a job file or a scene to a module registry.
//
// The persisted form is neutral (a JSON bool/number/string/triple) while the module
// declares a typed descriptor, so values are coerced by descriptor: text becomes an
// Enum label or an Asset path, an integer also satisfies a Float parameter, and a
// value whose kind cannot satisfy the descriptor is rejected by name. Transactional
// through `ParameterRegistry::applyOverrides`.
bool applyParameterOverrides(
    ParameterRegistry& parameters,
    const std::vector<ModuleParameterOverride>& overrides,
    std::string& error
);

class ModuleRuntime {
public:
    explicit ModuleRuntime(const ModuleRegistry& registry);

    // Creates the instance and applies its parameter defaults plus `overrides`.
    // `seed` is part of the run identity, not of the module instance.
    bool configure(
        const std::string& moduleId,
        const std::vector<ModuleParameterOverride>& overrides,
        std::uint32_t seed,
        std::string& error
    );

    // Drops the instance and returns to the idle state.
    void clear();
    bool active() const { return module_ != nullptr; }

    // Copies the edit scene into the runtime scene and runs `initialize`. Preview,
    // module failure and Reset never write back into the edit scene.
    bool reset(
        const Scene& editScene,
        int startFrame,
        int endFrame,
        int framesPerSecond,
        std::string& error
    );

    // Steps the timeline forward to `frame` with deterministic fixed steps. The
    // frame must be inside the configured range and not before the current frame.
    bool runToFrame(int frame, std::string& error);

    void applyPresentation(const CameraOrbitState& authoredCamera,
        const RendererSettings& authoredRenderer, CameraOrbitState& camera,
        RendererSettings& renderer) const;

    bool bake(std::string& error);

    // Sets the deterministic frame range without touching the runtime scene, so a host can
    // ask for `cacheKey` before deciding whether to run the module at all. `reset` applies
    // the same configuration.
    void configureTimeline(int startFrame, int endFrame, int framesPerSecond);

    // The exact inputs a cache entry must match to be reusable for this run.
    //
    // The overload takes the authored-scene hash explicitly so a host can classify a cache
    // before running the module; the no-argument form uses the hash captured by `reset`.
    // Both derive every other field from this runtime, so a caller cannot assemble a key
    // differently and silently mismatch a baked entry.
    SimulationCacheKey cacheKey(std::uint64_t sceneContentHash) const;
    SimulationCacheKey cacheKey() const;

    // Runs the module across `startFrame..endFrame` and records one deterministic cache
    // frame per step. Rebuilds from the authored scene first, so a bake never depends
    // on whatever the runtime happened to hold before.
    bool bakeSimulation(
        const Scene& editScene,
        int startFrame,
        int endFrame,
        int framesPerSecond,
        SimulationCache& cache,
        std::string& error
    );

    // Copies a cached frame into the runtime scene and verifies it by re-hashing the
    // result. Returns false when the cached entities do not reproduce the recorded
    // content hash, which means the cache must not be trusted.
    bool applyCachedFrame(const SimulationCacheFrame& frame, std::string& error);

    const ModuleRunReport& report() const { return report_; }
    RuntimeScene& runtimeScene() { return runtimeScene_; }
    const RuntimeScene& runtimeScene() const { return runtimeScene_; }
    const Timeline& timeline() const { return timeline_; }
    ParameterRegistry& parameters() { return parameters_; }
    const ParameterRegistry& parameters() const { return parameters_; }
    ISceneModule* module() { return module_.get(); }
    const std::vector<ModuleLogEntry>& logEntries() const { return context_.logEntries(); }

    // The host owns cancellation (GUI cancel, CLI Ctrl+C, job timeout).
    void setCancellationCheck(std::function<bool()> check) {
        context_.setCancellationCheck(std::move(check));
    }

private:
    // A module that logged an error has failed even when fixedUpdate returned true.
    bool failIfModuleLoggedError(std::string& error);

    const ModuleRegistry* registry_{nullptr};
    std::unique_ptr<ISceneModule> module_;
    ParameterRegistry parameters_;
    RuntimeScene runtimeScene_;
    Timeline timeline_;
    SceneContext context_;
    ModuleRunReport report_;
};
