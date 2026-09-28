#include "runtime/BatchRuntime.h"
#include "runtime/RenderJob.h"
#include "runtime/RenderQueue.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void writeJob(const std::filesystem::path& path,
              const std::filesystem::path& scene,
              const std::filesystem::path& outputStem,
              int samplesPerPixel = 1) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "{\n"
           << "  \"format\": \"MyRendererRenderJob\",\n"
           << "  \"schemaVersion\": 1,\n"
           << "  \"scene\": \"" << scene.generic_u8string() << "\",\n"
           << "  \"renderer\": \"cpu-path-traced\",\n"
           << "  \"camera\": \"scene\",\n"
           << "  \"resolution\": [8, 8],\n"
           << "  \"frames\": {\"start\": 0, \"end\": 0, \"fps\": 24},\n"
           << "  \"sampling\": {\"spp\": " << samplesPerPixel
           << ", \"maxDepth\": 2, \"seed\": 20260917},\n"
           << "  \"aovs\": [\"beauty\"],\n"
           << "  \"output\": {\"path\": \"" << outputStem.generic_u8string()
           << "\", \"formats\": [\"png\", \"hdr\"], \"resume\": false},\n"
           << "  \"simulationCache\": \"\",\n"
           << "  \"failurePolicy\": \"stop\"\n"
           << "}\n";
    require(static_cast<bool>(output), "Failed to write Render Queue fixture");
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    require(static_cast<bool>(output), "Failed to write fixture: " + path.string());
}

void writeActiveQueueState(const std::filesystem::path& path,
                           const std::filesystem::path& jobPath,
                           const std::string& status) {
    std::ostringstream json;
    json << "{\n"
         << "  \"format\": \"MyRendererRenderQueue\",\n"
         << "  \"schemaVersion\": 2,\n"
         << "  \"revision\": 17,\n"
         << "  \"sessionState\": \"active\",\n"
         << "  \"nextId\": 2,\n"
         << "  \"entries\": [{\n"
         << "    \"id\": 1,\n"
         << "    \"job\": \"" << jobPath.generic_u8string() << "\",\n"
         << "    \"status\": \"" << status << "\",\n"
         << "    \"currentFrame\": 0,\n"
         << "    \"completedFrames\": 0,\n"
         << "    \"skippedFrames\": 0,\n"
         << "    \"failedFrames\": 0,\n"
         << "    \"outputCount\": 0,\n"
         << "    \"message\": \"Interrupted fixture\"\n"
         << "  }]\n"
         << "}\n";
    writeText(path, json.str());
}

std::string readBinary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "Missing output: " + path.string());
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::string normalizedReport(const std::filesystem::path& path) {
    std::istringstream input(readBinary(path));
    std::ostringstream normalized;
    std::string line;
    while (std::getline(input, line)) {
        if (line.find("\"renderMilliseconds\"") == std::string::npos) normalized << line << '\n';
    }
    return normalized.str();
}

RenderQueueEntrySnapshot waitForTerminal(RenderQueue& queue, std::uint64_t id) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        queue.update();
        for (const auto& entry : queue.entries()) {
            if (entry.id != id) continue;
            if (entry.status == RenderQueueStatus::Complete
                || entry.status == RenderQueueStatus::Failed
                || entry.status == RenderQueueStatus::Cancelled) {
                return entry;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Timed out waiting for Render Queue completion");
}

} // namespace

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / "MyRendererRenderQueueAcceptance";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    try {
        std::filesystem::create_directories(root);
        const std::filesystem::path sourceRoot(MYRENDERER_SOURCE_DIR);
        const std::filesystem::path scene = sourceRoot / "assets" / "scenes" / "fixtures"
            / "01_multi_model_hierarchy.myscene";
        const std::filesystem::path jobPath = root / "parity.renderjob";
        const std::filesystem::path outputStem = root / "output" / "frame_{frame:04}";
        writeJob(jobPath, scene, outputStem);

        RenderJob directJob;
        std::string error;
        require(loadRenderJob(jobPath, directJob, error), error);
        const BatchSequenceResult direct = runRenderJobSequence(directJob);
        require(direct.status == BatchFrameStatus::Complete, direct.error);
        const std::filesystem::path frameStem = root / "output" / "frame_0000";
        const std::string directPng = readBinary(frameStem.string() + ".png");
        const std::string directHdr = readBinary(frameStem.string() + ".hdr");
        const std::string directReport = normalizedReport(frameStem.string() + "-report.json");
        std::filesystem::remove(frameStem.string() + ".png");
        std::filesystem::remove(frameStem.string() + ".hdr");
        std::filesystem::remove(frameStem.string() + "-report.json");

        {
            RenderQueue queue(root / "run-queue.json");
            std::uint64_t queuedId = 0U;
            require(queue.enqueue(jobPath, queuedId, error), error);
            const RenderQueueEntrySnapshot completed = waitForTerminal(queue, queuedId);
            require(completed.status == RenderQueueStatus::Complete, completed.message);
            require(completed.completedFrames == 1 && completed.outputCount == 3U,
                    "Render Queue result counters are incorrect");
            require(readBinary(frameStem.string() + ".png") == directPng,
                    "Queue and direct runtime PNG outputs differ");
            require(readBinary(frameStem.string() + ".hdr") == directHdr,
                    "Queue and direct runtime HDR outputs differ");
            require(normalizedReport(frameStem.string() + "-report.json") == directReport,
                    "Queue and direct runtime reports differ outside timing");
        }

        const std::filesystem::path secondJob = root / "second.renderjob";
        writeJob(secondJob, scene, root / "second" / "frame_{frame:04}");
        const std::filesystem::path persistence = root / "pending-queue.json";
        std::uint64_t firstId = 0U;
        std::uint64_t secondId = 0U;
        {
            RenderQueue queue(persistence);
            require(queue.enqueue(jobPath, firstId, error), error);
            require(queue.enqueue(secondJob, secondId, error), error);
            require(queue.movePending(secondId, -1, error), error);
            const auto reordered = queue.entries();
            require(reordered.size() == 2U && reordered[0].id == secondId
                    && reordered[1].id == firstId,
                    "Pending Render Jobs were not reordered");
        }
        {
            RenderQueue restored(persistence);
            require(restored.restore(error), error);
            require(restored.restoreSource() == RenderQueueRestoreSource::Primary,
                    "Clean Queue state did not restore from the primary file");
            require(restored.previousSession() == RenderQueuePreviousSession::CleanShutdown,
                    "Clean Queue shutdown was not distinguished from interruption");
            require(restored.restoreDiagnostic().find("clean shutdown") != std::string::npos,
                    "Clean Queue restore diagnostic is missing");
            const auto entries = restored.entries();
            require(entries.size() == 2U && entries[0].id == secondId
                    && entries[1].id == firstId,
                    "Persisted Pending order was not restored");
            require(entries[0].status == RenderQueueStatus::Pending
                    && entries[1].status == RenderQueueStatus::Pending,
                    "Pending state was not restored");
            require(restored.remove(firstId, error), error);
            require(restored.entries().size() == 1U,
                    "Pending Render Job was not removed");
        }

        const std::filesystem::path fallbackState = root / "fallback-queue.json";
        {
            RenderQueue queue(fallbackState);
            std::uint64_t id = 0U;
            require(queue.enqueue(jobPath, id, error), error);
            require(queue.enqueue(secondJob, id, error), error);
        }
        require(std::filesystem::is_regular_file(fallbackState.string() + ".bak"),
                "Render Queue did not retain a last-known-good backup");
        writeText(fallbackState, "{corrupt primary");
        {
            RenderQueue recovered(fallbackState);
            require(recovered.restore(error), error);
            require(recovered.restoreSource() == RenderQueueRestoreSource::Backup,
                    "Corrupt primary state did not fall back to the backup");
            require(recovered.restoreDiagnostic().find("from backup") != std::string::npos,
                    "Backup recovery diagnostic is missing");
            require(recovered.entries().size() == 2U,
                    "Backup recovery lost queued jobs");
        }

        for (const std::string status : {"Running", "Cancelling"}) {
            const std::filesystem::path activeState = root / (status + "-queue.json");
            writeActiveQueueState(activeState, jobPath, status);
            RenderQueue recovered(activeState);
            require(recovered.restore(error), error);
            require(recovered.restoreSource() == RenderQueueRestoreSource::Primary,
                    status + " state did not restore from primary");
            require(recovered.previousSession() == RenderQueuePreviousSession::Interrupted,
                    status + " state did not identify an interrupted session");
            const auto entries = recovered.entries();
            require(entries.size() == 1U && entries[0].status == RenderQueueStatus::Pending,
                    status + " state did not recover as Pending");
            require(entries[0].message.find("Recovered " + status + " job") != std::string::npos,
                    status + " recovery message is missing its original state");
        }

        const std::filesystem::path brokenState = root / "broken-queue.json";
        const std::filesystem::path brokenBackup = brokenState.string() + ".bak";
        writeText(brokenState, "{broken primary");
        writeText(brokenBackup, "{broken backup");
        const std::string brokenPrimaryBefore = readBinary(brokenState);
        const std::string brokenBackupBefore = readBinary(brokenBackup);
        {
            RenderQueue broken(brokenState);
            require(!broken.restore(error),
                    "Render Queue unexpectedly accepted two corrupt state files");
            require(error.find("primary and backup were preserved") != std::string::npos,
                    "Corrupt-state error does not promise preservation");
        }
        require(readBinary(brokenState) == brokenPrimaryBefore
                && readBinary(brokenBackup) == brokenBackupBefore,
                "Failed recovery overwrote forensic Queue state");

        std::filesystem::remove_all(root, cleanupError);
        std::cout << "Render Queue scheduling, crash recovery, and direct-runtime parity tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(root, cleanupError);
        std::cerr << "Render Queue tests failed: " << exception.what() << '\n';
        return 1;
    }
}
