#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "module/ModuleRuntime.h"
#include "pathtracer/ProgressiveRenderer.h"
#include "runtime/RenderJob.h"

class CancellationToken final {
  public:
    void requestCancellation() noexcept { requested_.store(true); }
    bool isCancellationRequested() const noexcept { return requested_.load(); }
    const std::atomic<bool>* nativeFlag() const noexcept { return &requested_; }

  private:
    std::atomic<bool> requested_{false};
};

enum class BatchFrameStatus {
    Pending,
    Running,
    Skipped,
    Cancelled,
    Failed,
    Complete
};

enum class BatchOutputDiagnosticCode {
    StalePartial,
    MissingReport,
    IncompleteCommit,
    FormatSetMismatch,
    AovSetMismatch,
    InvalidReport,
    ManifestMismatch,
    UnexpectedManagedArtifact
};

enum class BatchOutputRecoveryAction {
    RefuseOverwrite,
    CleanAndRerender
};

struct BatchOutputDiagnostic {
    int frame{0};
    BatchOutputDiagnosticCode code{BatchOutputDiagnosticCode::IncompleteCommit};
    BatchOutputRecoveryAction action{BatchOutputRecoveryAction::RefuseOverwrite};
    std::vector<std::filesystem::path> paths;
    std::string message;
};

const char* batchOutputDiagnosticCodeName(BatchOutputDiagnosticCode code);
const char* batchOutputRecoveryActionName(BatchOutputRecoveryAction action);

struct BatchFrameResult {
    int frame{0};
    bool skipped{false};
    BatchFrameStatus status{BatchFrameStatus::Pending};
    pathtracer::RenderStatistics statistics;
    // Serialized backend evidence; empty for the existing CPU path.
    std::string gpuBackendReport;
    std::vector<std::filesystem::path> outputs;
    std::vector<BatchOutputDiagnostic> outputDiagnostics;
    // Empty when the job drives the scene directly, otherwise the module run that
    // produced this frame (id, api version, build id, seed, content hash, state).
    ModuleRunReport module;
    // Whether this frame was simulated or reused from a verified cache entry.
    SimulationCacheStatus cacheStatus{SimulationCacheStatus::Disabled};
    std::string cacheMessage;
};

// Bakes a Render Job's module over its whole frame range into a deterministic cache.
bool bakeJobSimulation(
    const RenderJob& job,
    const ModuleRegistry* modules,
    SimulationCache& cache,
    std::string& error,
    const CancellationToken* cancellation = nullptr
);

struct BatchSequenceProgress {
    int frame{0};
    int completedFrames{0};
    int totalFrames{0};
    BatchFrameStatus frameStatus{BatchFrameStatus::Pending};
    std::string message;
};

struct BatchSequenceResult {
    BatchFrameStatus status{BatchFrameStatus::Pending};
    int lastFrame{0};
    int completedFrames{0};
    int skippedFrames{0};
    int failedFrames{0};
    int totalFrames{0};
    std::size_t outputCount{0U};
    int recoveredFrames{0};
    std::vector<BatchOutputDiagnostic> outputDiagnostics;
    std::string error;
};

using BatchSequenceProgressCallback = std::function<void(const BatchSequenceProgress&)>;

bool validateRenderJobAssets(const RenderJob& job, std::string& error,
                             const ModuleRegistry* modules = nullptr);
bool runRenderJobFrame(const RenderJob& job, int frame, BatchFrameResult& result,
                       std::string& error, const CancellationToken* cancellation = nullptr,
                       const ModuleRegistry* modules = nullptr);
BatchSequenceResult runRenderJobSequence(
    const RenderJob& job,
    const CancellationToken* cancellation = nullptr,
    const BatchSequenceProgressCallback& progress = {},
    const ModuleRegistry* modules = nullptr
);
