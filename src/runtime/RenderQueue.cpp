#include "runtime/RenderQueue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#define RAPIDJSON_NAMESPACE myrenderer_render_queue_json
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#undef RAPIDJSON_NAMESPACE
#undef RAPIDJSON_NAMESPACE_BEGIN
#undef RAPIDJSON_NAMESPACE_END

namespace queue_json = myrenderer_render_queue_json;

namespace {

struct QueueProgress {
    std::atomic<BatchFrameStatus> frameStatus{BatchFrameStatus::Pending};
    std::atomic<int> frame{0};
    std::atomic<int> completedFrames{0};
};

RenderQueueStatus statusFromName(const std::string& value) {
    if (value == "Pending") return RenderQueueStatus::Pending;
    if (value == "Running") return RenderQueueStatus::Running;
    if (value == "Cancelling") return RenderQueueStatus::Cancelling;
    if (value == "Cancelled") return RenderQueueStatus::Cancelled;
    if (value == "Failed") return RenderQueueStatus::Failed;
    if (value == "Complete") return RenderQueueStatus::Complete;
    throw std::runtime_error("Unknown Render Queue status: " + value);
}

bool isActive(RenderQueueStatus status) {
    return status == RenderQueueStatus::Running || status == RenderQueueStatus::Cancelling;
}

std::filesystem::path withSuffix(const std::filesystem::path& path, const char* suffix) {
    return std::filesystem::path(path.string() + suffix);
}

void replaceStateFile(const std::filesystem::path& temporary,
                      const std::filesystem::path& target,
                      const std::filesystem::path& backup) {
#ifdef _WIN32
    if (std::filesystem::is_regular_file(target)) {
        std::error_code ignored;
        std::filesystem::remove(backup, ignored);
        if (!ReplaceFileW(target.c_str(), temporary.c_str(), backup.c_str(),
                          REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
            throw std::runtime_error("Atomic Render Queue replacement failed with Windows error "
                + std::to_string(GetLastError()));
        }
    } else if (!MoveFileExW(temporary.c_str(), target.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("Render Queue state install failed with Windows error "
            + std::to_string(GetLastError()));
    }
#else
    if (std::filesystem::is_regular_file(target)) {
        const std::filesystem::path backupTemporary = withSuffix(backup, ".partial");
        std::filesystem::copy_file(target, backupTemporary,
                                   std::filesystem::copy_options::overwrite_existing);
        std::error_code ignored;
        std::filesystem::remove(backup, ignored);
        std::filesystem::rename(backupTemporary, backup);
    }
    std::filesystem::rename(temporary, target);
#endif
}

void repairPrimaryFromBackup(const std::filesystem::path& backup,
                             const std::filesystem::path& primary) {
    const std::filesystem::path temporary = withSuffix(primary, ".partial");
    std::filesystem::copy_file(backup, temporary,
                               std::filesystem::copy_options::overwrite_existing);
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), primary.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("Render Queue backup repair failed with Windows error "
            + std::to_string(GetLastError()));
    }
#else
    std::filesystem::rename(temporary, primary);
#endif
}

} // namespace

struct RenderQueue::Entry {
    std::uint64_t id{0U};
    std::filesystem::path jobPath;
    RenderJob job;
    RenderQueueStatus status{RenderQueueStatus::Pending};
    int currentFrame{0};
    int completedFrames{0};
    int skippedFrames{0};
    int failedFrames{0};
    std::size_t outputCount{0U};
    std::string message;
    std::shared_ptr<CancellationToken> cancellation;
    std::shared_ptr<QueueProgress> progress;
    std::future<BatchSequenceResult> future;
};

const char* renderQueueStatusName(RenderQueueStatus status) {
    switch (status) {
        case RenderQueueStatus::Pending: return "Pending";
        case RenderQueueStatus::Running: return "Running";
        case RenderQueueStatus::Cancelling: return "Cancelling";
        case RenderQueueStatus::Cancelled: return "Cancelled";
        case RenderQueueStatus::Failed: return "Failed";
        case RenderQueueStatus::Complete: return "Complete";
    }
    return "Unknown";
}

RenderQueue::RenderQueue(std::filesystem::path persistencePath)
    : persistencePath_(std::move(persistencePath)) {
}

std::filesystem::path RenderQueue::backupPath() const {
    return persistencePath_.empty() ? std::filesystem::path{} : withSuffix(persistencePath_, ".bak");
}

RenderQueue::~RenderQueue() {
    shutdown();
}

RenderQueue::Entry* RenderQueue::find(std::uint64_t id) {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const auto& entry) {
        return entry->id == id;
    });
    return found == entries_.end() ? nullptr : found->get();
}

const RenderQueue::Entry* RenderQueue::find(std::uint64_t id) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const auto& entry) {
        return entry->id == id;
    });
    return found == entries_.end() ? nullptr : found->get();
}

bool RenderQueue::enqueue(const std::filesystem::path& jobPath, std::uint64_t& id,
                          std::string& error) {
    RenderJob job;
    if (!loadRenderJob(jobPath, job, error)) return false;
    if (!validateQueueRenderJob(job, error)) return false;

    auto entry = std::make_unique<Entry>();
    entry->id = nextId_++;
    entry->jobPath = job.sourcePath;
    entry->job = std::move(job);
    entry->currentFrame = entry->job.startFrame;
    entry->message = "Waiting for the queue worker.";
    id = entry->id;
    entries_.push_back(std::move(entry));
    if (!persist(error)) {
        entries_.pop_back();
        --nextId_;
        return false;
    }
    error.clear();
    return true;
}

bool RenderQueue::movePending(std::uint64_t id, int direction, std::string& error) {
    if (direction != -1 && direction != 1) {
        error = "Render Queue move direction must be -1 or 1";
        return false;
    }
    const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const auto& entry) {
        return entry->id == id;
    });
    if (found == entries_.end() || (*found)->status != RenderQueueStatus::Pending) {
        error = "Only a Pending Render Job can be reordered";
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(std::distance(entries_.begin(), found));
    std::size_t target = index;
    if (direction < 0) {
        for (std::size_t candidate = index; candidate > 0U; --candidate) {
            if (entries_[candidate - 1U]->status == RenderQueueStatus::Pending) {
                target = candidate - 1U;
                break;
            }
        }
    } else {
        for (std::size_t candidate = index + 1U; candidate < entries_.size(); ++candidate) {
            if (entries_[candidate]->status == RenderQueueStatus::Pending) {
                target = candidate;
                break;
            }
        }
    }
    if (target == index) {
        error = "Render Job is already at the Pending queue boundary";
        return false;
    }
    std::swap(entries_[index], entries_[target]);
    return persist(error);
}

bool RenderQueue::remove(std::uint64_t id, std::string& error) {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const auto& entry) {
        return entry->id == id;
    });
    if (found == entries_.end()) {
        error = "Render Queue entry does not exist";
        return false;
    }
    if (isActive((*found)->status)) {
        error = "An active Render Job must be cancelled before removal";
        return false;
    }
    entries_.erase(found);
    return persist(error);
}

bool RenderQueue::retry(std::uint64_t id, std::string& error) {
    Entry* entry = find(id);
    if (entry == nullptr) {
        error = "Render Queue entry does not exist";
        return false;
    }
    if (entry->status != RenderQueueStatus::Failed
        && entry->status != RenderQueueStatus::Cancelled) {
        error = "Only a Failed or Cancelled Render Job can be retried";
        return false;
    }
    RenderJob refreshed;
    if (!loadRenderJob(entry->jobPath, refreshed, error)) return false;
    if (!validateQueueRenderJob(refreshed, error)) return false;
    entry->job = std::move(refreshed);
    entry->status = RenderQueueStatus::Pending;
    entry->currentFrame = entry->job.startFrame;
    entry->completedFrames = 0;
    entry->skippedFrames = 0;
    entry->failedFrames = 0;
    entry->outputCount = 0U;
    entry->message = "Retry pending.";
    return persist(error);
}

bool RenderQueue::cancel(std::uint64_t id, std::string& error) {
    Entry* entry = find(id);
    if (entry == nullptr || id != activeId_ || entry->status != RenderQueueStatus::Running) {
        error = "Render Job is not currently running";
        return false;
    }
    entry->status = RenderQueueStatus::Cancelling;
    entry->message = "Cancellation requested; waiting for a safe render-pass boundary.";
    entry->cancellation->requestCancellation();
    return persist(error);
}

bool RenderQueue::cancelActive(std::string& error) {
    if (activeId_ == 0U) {
        error = "Render Queue has no active job";
        return false;
    }
    return cancel(activeId_, error);
}

void RenderQueue::startNext() {
    if (activeId_ != 0U || shutdownComplete_) return;
    const auto found = std::find_if(entries_.begin(), entries_.end(), [](const auto& entry) {
        return entry->status == RenderQueueStatus::Pending;
    });
    if (found == entries_.end()) return;

    Entry& entry = **found;
    entry.status = RenderQueueStatus::Running;
    entry.message = "Running on the shared Batch Sequence Runtime.";
    entry.cancellation = std::make_shared<CancellationToken>();
    entry.progress = std::make_shared<QueueProgress>();
    entry.progress->frame.store(entry.job.startFrame);
    const RenderJob job = entry.job;
    const auto cancellation = entry.cancellation;
    const auto progress = entry.progress;
    const ModuleRegistry* modules = modules_;
    entry.future = std::async(std::launch::async, [job, cancellation, progress, modules] {
        return runRenderJobSequence(
            job,
            cancellation.get(),
            [progress](const BatchSequenceProgress& update) {
                progress->frame.store(update.frame);
                progress->completedFrames.store(update.completedFrames);
                progress->frameStatus.store(update.frameStatus);
            },
            modules
        );
    });
    activeId_ = entry.id;
    std::string ignored;
    persist(ignored);
}

void RenderQueue::update() {
    if (shutdownComplete_) return;
    if (activeId_ != 0U) {
        Entry* entry = find(activeId_);
        if (entry == nullptr) {
            activeId_ = 0U;
        } else {
            entry->currentFrame = entry->progress->frame.load();
            entry->completedFrames = entry->progress->completedFrames.load();
            if (entry->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

            BatchSequenceResult result;
            try {
                result = entry->future.get();
            } catch (const std::exception& exception) {
                result.status = BatchFrameStatus::Failed;
                result.error = exception.what();
            }
            entry->currentFrame = result.lastFrame;
            entry->completedFrames = result.completedFrames;
            entry->skippedFrames = result.skippedFrames;
            entry->failedFrames = result.failedFrames;
            entry->outputCount = result.outputCount;
            if (result.status == BatchFrameStatus::Complete) {
                entry->status = RenderQueueStatus::Complete;
                entry->message = result.recoveredFrames > 0
                    ? "Complete; safely cleaned and re-rendered "
                        + std::to_string(result.recoveredFrames) + " partial output set(s)."
                    : "Complete.";
            } else if (result.status == BatchFrameStatus::Cancelled) {
                entry->status = RenderQueueStatus::Cancelled;
                entry->message = "Cancelled; the interrupted frame published no final output.";
            } else {
                entry->status = RenderQueueStatus::Failed;
                entry->message = result.error;
            }
            entry->cancellation.reset();
            entry->progress.reset();
            activeId_ = 0U;
            std::string ignored;
            persist(ignored);
        }
    }
    startNext();
}

std::vector<RenderQueueEntrySnapshot> RenderQueue::entries() const {
    std::vector<RenderQueueEntrySnapshot> snapshots;
    snapshots.reserve(entries_.size());
    for (const auto& entry : entries_) {
        RenderQueueEntrySnapshot snapshot;
        snapshot.id = entry->id;
        snapshot.jobPath = entry->jobPath;
        snapshot.status = entry->status;
        snapshot.currentFrame = entry->progress != nullptr
            ? entry->progress->frame.load()
            : entry->currentFrame;
        snapshot.startFrame = entry->job.startFrame;
        snapshot.endFrame = entry->job.endFrame;
        snapshot.framesPerSecond = entry->job.framesPerSecond;
        snapshot.completedFrames = entry->progress != nullptr
            ? entry->progress->completedFrames.load()
            : entry->completedFrames;
        snapshot.skippedFrames = entry->skippedFrames;
        snapshot.failedFrames = entry->failedFrames;
        snapshot.outputCount = entry->outputCount;
        snapshot.message = entry->message;
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

bool RenderQueue::persist(std::string& error) {
    if (persistencePath_.empty()) {
        error.clear();
        return true;
    }
    if (persistenceBlocked_) {
        error = "Render Queue persistence is blocked because state recovery failed";
        return false;
    }
    try {
        if (!persistencePath_.parent_path().empty()) {
            std::filesystem::create_directories(persistencePath_.parent_path());
        }
        const std::uint64_t nextRevision = revision_ + 1U;
        queue_json::StringBuffer buffer;
        queue_json::Writer<queue_json::StringBuffer> writer(buffer);
        writer.StartObject();
        writer.Key("format"); writer.String("MyRendererRenderQueue");
        writer.Key("schemaVersion"); writer.Int(2);
        writer.Key("revision"); writer.Uint64(nextRevision);
        writer.Key("sessionState"); writer.String(shutdownComplete_ ? "clean-shutdown" : "active");
        writer.Key("nextId"); writer.Uint64(nextId_);
        writer.Key("entries"); writer.StartArray();
        for (const auto& entry : entries_) {
            writer.StartObject();
            writer.Key("id"); writer.Uint64(entry->id);
            writer.Key("job"); writer.String(entry->jobPath.generic_u8string().c_str());
            writer.Key("status"); writer.String(renderQueueStatusName(entry->status));
            writer.Key("currentFrame"); writer.Int(entry->currentFrame);
            writer.Key("completedFrames"); writer.Int(entry->completedFrames);
            writer.Key("skippedFrames"); writer.Int(entry->skippedFrames);
            writer.Key("failedFrames"); writer.Int(entry->failedFrames);
            writer.Key("outputCount"); writer.Uint64(static_cast<std::uint64_t>(entry->outputCount));
            writer.Key("message"); writer.String(entry->message.c_str());
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();

        const std::filesystem::path temporary = withSuffix(persistencePath_, ".partial");
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write Render Queue state");
        output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
        output.flush();
        output.close();
        if (!output) throw std::runtime_error("Failed while writing Render Queue state");
        replaceStateFile(temporary, persistencePath_, backupPath());
        revision_ = nextRevision;
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        std::error_code ignored;
        std::filesystem::remove(withSuffix(persistencePath_, ".partial"), ignored);
        error = exception.what();
        return false;
    }
}

bool RenderQueue::restore(std::string& error) {
    if (persistencePath_.empty()) {
        error.clear();
        return true;
    }
    if (activeId_ != 0U || !entries_.empty()) {
        error = "Render Queue restore requires an empty queue";
        return false;
    }
    struct LoadedState {
        std::uint64_t nextId{1U};
        std::uint64_t revision{0U};
        RenderQueuePreviousSession previousSession{RenderQueuePreviousSession::Unknown};
        std::vector<std::unique_ptr<Entry>> entries;
    };
    const auto loadState = [&](const std::filesystem::path& path, LoadedState& loaded,
                               std::string& loadError) -> bool {
        try {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot open state file");
            const std::string json((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
            queue_json::Document root;
            root.Parse(json.c_str(), json.size());
            if (root.HasParseError()) {
                throw std::runtime_error(std::string("JSON parse error: ")
                    + queue_json::GetParseError_En(root.GetParseError()));
            }
            if (!root.IsObject() || !root.HasMember("format") || !root["format"].IsString()
                || std::string(root["format"].GetString()) != "MyRendererRenderQueue"
                || !root.HasMember("schemaVersion") || !root["schemaVersion"].IsInt()
                || !root.HasMember("nextId") || !root["nextId"].IsUint64()
                || !root.HasMember("entries") || !root["entries"].IsArray()) {
                throw std::runtime_error("state header is invalid");
            }
            const int schemaVersion = root["schemaVersion"].GetInt();
            if (schemaVersion != 1 && schemaVersion != 2) {
                throw std::runtime_error("unsupported schemaVersion "
                    + std::to_string(schemaVersion));
            }
            LoadedState candidate;
            candidate.nextId = root["nextId"].GetUint64();
            if (schemaVersion == 2) {
                if (!root.HasMember("revision") || !root["revision"].IsUint64()
                    || !root.HasMember("sessionState") || !root["sessionState"].IsString()) {
                    throw std::runtime_error("schema 2 lifecycle metadata is invalid");
                }
                candidate.revision = root["revision"].GetUint64();
                const std::string sessionState = root["sessionState"].GetString();
                if (sessionState == "clean-shutdown") {
                    candidate.previousSession = RenderQueuePreviousSession::CleanShutdown;
                } else if (sessionState == "active") {
                    candidate.previousSession = RenderQueuePreviousSession::Interrupted;
                } else {
                    throw std::runtime_error("unknown sessionState " + sessionState);
                }
            }
            for (const auto& value : root["entries"].GetArray()) {
                if (!value.IsObject() || !value.HasMember("id") || !value["id"].IsUint64()
                    || !value.HasMember("job") || !value["job"].IsString()
                    || !value.HasMember("status") || !value["status"].IsString()) {
                    throw std::runtime_error("queue entry is invalid");
                }
                auto entry = std::make_unique<Entry>();
                entry->id = value["id"].GetUint64();
                entry->jobPath = std::filesystem::u8path(value["job"].GetString());
                const RenderQueueStatus persistedStatus = statusFromName(value["status"].GetString());
                entry->status = persistedStatus;
                std::string jobError;
                if (!loadRenderJob(entry->jobPath, entry->job, jobError)
                    || !validateQueueRenderJob(entry->job, jobError)) {
                    entry->status = RenderQueueStatus::Failed;
                    entry->message = "Recovered Job is invalid: " + jobError;
                } else {
                    entry->currentFrame = value.HasMember("currentFrame")
                        && value["currentFrame"].IsInt()
                        ? value["currentFrame"].GetInt() : entry->job.startFrame;
                    entry->completedFrames = value.HasMember("completedFrames")
                        && value["completedFrames"].IsInt()
                        ? value["completedFrames"].GetInt() : 0;
                    entry->skippedFrames = value.HasMember("skippedFrames")
                        && value["skippedFrames"].IsInt()
                        ? value["skippedFrames"].GetInt() : 0;
                    entry->failedFrames = value.HasMember("failedFrames")
                        && value["failedFrames"].IsInt()
                        ? value["failedFrames"].GetInt() : 0;
                    entry->outputCount = value.HasMember("outputCount")
                        && value["outputCount"].IsUint64()
                        ? static_cast<std::size_t>(value["outputCount"].GetUint64()) : 0U;
                    entry->message = value.HasMember("message") && value["message"].IsString()
                        ? value["message"].GetString() : "Restored.";
                    if (isActive(persistedStatus)) {
                        entry->status = RenderQueueStatus::Pending;
                        const char* cause = candidate.previousSession
                                == RenderQueuePreviousSession::Interrupted
                            ? "interrupted application session"
                            : (candidate.previousSession == RenderQueuePreviousSession::CleanShutdown
                                ? "inconsistent clean-shutdown state" : "legacy queue state");
                        entry->message = std::string("Recovered ")
                            + renderQueueStatusName(persistedStatus) + " job after " + cause
                            + "; completed frames will resume.";
                    }
                }
                candidate.nextId = std::max(candidate.nextId, entry->id + 1U);
                candidate.entries.push_back(std::move(entry));
            }
            loaded = std::move(candidate);
            loadError.clear();
            return true;
        } catch (const std::exception& exception) {
            loadError = path.string() + ": " + exception.what();
            return false;
        }
    };

    const bool primaryExists = std::filesystem::is_regular_file(persistencePath_);
    const std::filesystem::path backup = backupPath();
    const bool backupExists = std::filesystem::is_regular_file(backup);
    if (!primaryExists && !backupExists) {
        if (!persist(error)) {
            persistenceBlocked_ = true;
            return false;
        }
        error.clear();
        return true;
    }

    LoadedState loaded;
    std::string primaryError;
    std::string backupError;
    if (primaryExists && loadState(persistencePath_, loaded, primaryError)) {
        restoreSource_ = RenderQueueRestoreSource::Primary;
    } else if (backupExists && loadState(backup, loaded, backupError)) {
        restoreSource_ = RenderQueueRestoreSource::Backup;
        if (!primaryExists) primaryError = persistencePath_.string() + ": state file is missing";
        try {
            repairPrimaryFromBackup(backup, persistencePath_);
        } catch (const std::exception& exception) {
            persistenceBlocked_ = true;
            error = "Render Queue backup is valid but primary repair failed: "
                + std::string(exception.what());
            return false;
        }
    } else {
        persistenceBlocked_ = true;
        error = "Render Queue recovery failed; primary and backup were preserved. Primary: "
            + (primaryError.empty() ? "missing" : primaryError) + "; backup: "
            + (backupError.empty() ? "missing" : backupError);
        return false;
    }

    nextId_ = loaded.nextId;
    revision_ = loaded.revision;
    previousSession_ = loaded.previousSession;
    entries_ = std::move(loaded.entries);
    if (restoreSource_ == RenderQueueRestoreSource::Backup) {
        restoreDiagnostic_ = "Recovered Render Queue from backup after primary failure: "
            + primaryError;
    } else if (previousSession_ == RenderQueuePreviousSession::Interrupted) {
        restoreDiagnostic_ = "Recovered Render Queue after an interrupted application session.";
    } else if (previousSession_ == RenderQueuePreviousSession::CleanShutdown) {
        restoreDiagnostic_ = "Restored Render Queue after a clean shutdown.";
    } else {
        restoreDiagnostic_ = "Restored legacy Render Queue state and upgraded it to schema 2.";
    }

    if (!persist(error)) {
        entries_.clear();
        nextId_ = 1U;
        persistenceBlocked_ = true;
        error = "Render Queue loaded but the active-session marker could not be committed: " + error;
        return false;
    }
    error.clear();
    return true;
}

void RenderQueue::shutdown() {
    if (shutdownComplete_) return;
    shutdownComplete_ = true;
    if (activeId_ != 0U) {
        Entry* entry = find(activeId_);
        if (entry != nullptr) {
            entry->cancellation->requestCancellation();
            if (entry->future.valid()) {
                entry->future.wait();
                try {
                    entry->future.get();
                } catch (...) {
                }
            }
            entry->status = RenderQueueStatus::Pending;
            entry->message = "Recovered after application shutdown; completed frames will resume.";
            entry->cancellation.reset();
            entry->progress.reset();
        }
        activeId_ = 0U;
    }
    std::string ignored;
    persist(ignored);
}
