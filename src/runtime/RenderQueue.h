#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "runtime/BatchRuntime.h"

enum class RenderQueueStatus {
    Pending,
    Running,
    Cancelling,
    Cancelled,
    Failed,
    Complete
};

enum class RenderQueueRestoreSource {
    None,
    Primary,
    Backup
};

enum class RenderQueuePreviousSession {
    Unknown,
    CleanShutdown,
    Interrupted
};

const char* renderQueueStatusName(RenderQueueStatus status);

struct RenderQueueEntrySnapshot {
    std::uint64_t id{0U};
    std::filesystem::path jobPath;
    RenderQueueStatus status{RenderQueueStatus::Pending};
    int currentFrame{0};
    int startFrame{0};
    int endFrame{0};
    int framesPerSecond{24};
    int completedFrames{0};
    int skippedFrames{0};
    int failedFrames{0};
    std::size_t outputCount{0U};
    std::string message;
};

class RenderQueue final {
  public:
    explicit RenderQueue(std::filesystem::path persistencePath = {});
    ~RenderQueue();

    RenderQueue(const RenderQueue&) = delete;
    RenderQueue& operator=(const RenderQueue&) = delete;

    bool restore(std::string& error);
    bool enqueue(const std::filesystem::path& jobPath, std::uint64_t& id, std::string& error);
    bool movePending(std::uint64_t id, int direction, std::string& error);
    bool remove(std::uint64_t id, std::string& error);
    bool retry(std::uint64_t id, std::string& error);
    bool cancel(std::uint64_t id, std::string& error);
    bool cancelActive(std::string& error);

    void update();
    void shutdown();

    // The GUI owns the module registry; the queue only forwards it, so a queued job
    // resolves modules through exactly the same registry as the preview.
    void setModuleRegistry(const ModuleRegistry* modules) { modules_ = modules; }
    using RasterRunner = std::function<BatchSequenceResult(const RenderJob&,
        const CancellationToken*, const BatchSequenceProgressCallback&)>;
    // Inject an executor that owns its own GL context (the GUI uses a child process).
    void setRasterRunner(RasterRunner runner) { rasterRunner_ = std::move(runner); }
    bool hasActiveJob() const { return activeId_ != 0U; }
    std::uint64_t activeId() const { return activeId_; }
    std::vector<RenderQueueEntrySnapshot> entries() const;
    const std::filesystem::path& persistencePath() const { return persistencePath_; }
    std::filesystem::path backupPath() const;
    RenderQueueRestoreSource restoreSource() const { return restoreSource_; }
    RenderQueuePreviousSession previousSession() const { return previousSession_; }
    const std::string& restoreDiagnostic() const { return restoreDiagnostic_; }

  private:
    struct Entry;

    Entry* find(std::uint64_t id);
    const Entry* find(std::uint64_t id) const;
    void startNext();
    bool persist(std::string& error);

    std::filesystem::path persistencePath_;
    const ModuleRegistry* modules_{nullptr};
    RasterRunner rasterRunner_;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::uint64_t nextId_{1U};
    std::uint64_t activeId_{0U};
    std::uint64_t revision_{0U};
    bool shutdownComplete_{false};
    bool persistenceBlocked_{false};
    RenderQueueRestoreSource restoreSource_{RenderQueueRestoreSource::None};
    RenderQueuePreviousSession previousSession_{RenderQueuePreviousSession::Unknown};
    std::string restoreDiagnostic_;
};
