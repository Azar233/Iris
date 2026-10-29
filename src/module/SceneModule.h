#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "module/ParameterRegistry.h"
#include "module/RuntimeScene.h"
#include "runtime/Timeline.h"

struct CameraOrbitState;
struct RendererSettings;

// Minimal C++ module contract for the rendering / simulation workbench.
//
// A module is statically compiled into `MyRendererModules` and created through the
// explicit `ModuleRegistry` by stable string id. It never owns an editor widget, an
// OpenGL / Vulkan context, a background thread or a job lifetime: everything it may
// touch arrives through the restricted `SceneContext` below, which only exposes the
// discardable runtime scene, the deterministic timeline, its own parameters and
// structured logging.

inline constexpr int moduleApiVersion = 1;

// Scene modules transform the runtime scene for a frame; simulation modules own a
// deterministic solver. Both use the same minimal lifecycle in this version.
enum class ModuleKind {
    Scene = 0,
    Simulation
};

const char* moduleKindName(ModuleKind kind);

struct ModuleManifest {
    std::string id;
    std::string displayName;
    ModuleKind kind{ModuleKind::Scene};
    std::string cmakeTarget;
    std::string sourceRoot;
    int apiVersion{moduleApiVersion};
    // Injected by CMake so a stale cache or report can be attributed to one build.
    std::string buildId;
};

enum class ModuleLogSeverity {
    Info = 0,
    Warning,
    Error
};

const char* moduleLogSeverityName(ModuleLogSeverity severity);

struct ModuleLogEntry {
    ModuleLogSeverity severity{ModuleLogSeverity::Info};
    int frame{0};
    std::string message;
};

class SceneContext {
public:
    SceneContext(
        RuntimeScene& runtimeScene,
        const Timeline& timeline,
        ParameterRegistry& parameters,
        std::uint32_t seed
    );

    // Only the discardable runtime copy is reachable here; the edit scene is not.
    Scene& scene() { return runtimeScene_->scene(); }
    const Scene& scene() const { return runtimeScene_->scene(); }
    RuntimeScene& runtimeScene() { return *runtimeScene_; }
    const RuntimeScene& runtimeScene() const { return *runtimeScene_; }

    const Timeline& timeline() const { return *timeline_; }
    ParameterRegistry& parameters() { return *parameters_; }
    const ParameterRegistry& parameters() const { return *parameters_; }

    std::uint32_t seed() const { return seed_; }

    // The host owns cancellation (GUI cancel button, CLI Ctrl+C, job timeout). A
    // module polls this between steps instead of owning a token or a thread.
    void setCancellationCheck(std::function<bool()> check) {
        cancellationCheck_ = std::move(check);
    }
    bool cancellationRequested() const {
        return cancellationCheck_ && cancellationCheck_();
    }

    // Diagnostic side channel. Logging is deliberately allowed from a const context
    // so a read-only `bake` can still report what it wrote.
    void log(ModuleLogSeverity severity, std::string message) const;
    const std::vector<ModuleLogEntry>& logEntries() const { return logEntries_; }
    void clearLog() const { logEntries_.clear(); }
    bool hasErrors() const;

private:
    // Held by pointer so the context stays copy-assignable: ModuleRuntime recreates it
    // whenever the seed or the runtime scene changes.
    RuntimeScene* runtimeScene_{nullptr};
    const Timeline* timeline_{nullptr};
    ParameterRegistry* parameters_{nullptr};
    std::uint32_t seed_{0U};
    std::function<bool()> cancellationCheck_;
    mutable std::vector<ModuleLogEntry> logEntries_;
};

struct ModuleSimulationStatistics {
    bool measured{false};
    std::uint64_t totalSubsteps{0};
    std::uint64_t lastAdvanceSubsteps{0};
    std::size_t bodyCount{0};
    double lastAdvanceMilliseconds{0.0};
};

struct ModuleSimulationConfiguration {
    std::string sdkVersion;
    std::string solver;
    std::string cacheSemantics;
    double fixedStepSeconds{0.0};
    float gravityY{0.0f};
    int substepsPerFrame{0};
    int positionIterations{0};
    int velocityIterations{0};
    std::size_t bodyCount{0}, dynamicBodyCount{0};
};

class ISceneModule {
public:
    virtual ~ISceneModule() = default;

    virtual ModuleManifest manifest() const = 0;

    // Declares the parameter schema once. The Inspector and `.myscene` overrides
    // read it from the registry, so no widget list is hard-coded per module.
    virtual void registerParameters(ParameterRegistry& parameters) = 0;

    // Called once after the runtime scene exists. Read-only: capture baseline state
    // here instead of mutating the scene before the first fixedUpdate.
    virtual bool initialize(const SceneContext& context, std::string& error) = 0;

    // Returns the module to its pre-initialize state. The host re-creates the
    // runtime scene from the edit scene, so a reset never leaks module output.
    virtual void reset() = 0;

    // One deterministic step. The same frame index, seed and parameters must always
    // produce the same runtime scene; nothing here may read wall-clock time.
    virtual bool fixedUpdate(
        SceneContext& context,
        double fixedDeltaSeconds,
        std::string& error
    ) = 0;

    // Presentation output is evaluated from the authored baseline for each frame.
    // This keeps editor controls and scene files untouched while allowing a module
    // to animate camera, sky and water through the same timeline as entity motion.
    virtual void applyPresentation(const SceneContext& context,
        const CameraOrbitState& authoredCamera, const RendererSettings& authoredRenderer,
        CameraOrbitState& camera, RendererSettings& renderer) const;

    // Optional deterministic cache write. Returning false with an empty error means
    // "nothing to bake".
    virtual bool bake(const SceneContext& context, std::string& error) {
        (void)context;
        (void)error;
        return true;
    }

    // Optional deterministic module state for the simulation cache / reports.
    virtual std::string serializeState() const { return {}; }
    // Diagnostic only: timings must never enter cache identity or deterministic state.
    virtual ModuleSimulationStatistics simulationStatistics() const { return {}; }
    virtual ModuleSimulationConfiguration simulationConfiguration() const { return {}; }
    virtual bool deserializeState(const std::string& state, std::string& error) {
        (void)state;
        (void)error;
        return true;
    }
};
