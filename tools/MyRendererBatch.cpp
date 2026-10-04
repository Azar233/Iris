#include "module/ModuleRegistry.h"
#include "runtime/BatchRuntime.h"
#include "runtime/RenderJob.h"

#include <csignal>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

CancellationToken cancellationToken;

void handleInterrupt(int) {
    cancellationToken.requestCancellation();
}

void usage() {
    std::cerr << "Usage:\n"
              << "  IrisBatch validate <job.renderjob>\n"
              << "  IrisBatch render-frame <job.renderjob> [frame] [--output <stem>]\n"
              << "  IrisBatch render-sequence <job.renderjob> [--output <pattern>]\n"
              << "  IrisBatch simulate <job.renderjob>\n"
              << "  IrisBatch bake <job.renderjob>\n";
}

int parseFrame(const char* text) {
    const std::string value(text);
    std::size_t parsed = 0U;
    const int frame = std::stoi(value, &parsed);
    if (parsed != value.size() || frame < 0) throw std::invalid_argument("Frame must be non-negative");
    return frame;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 64;
    }
    try {
        // The statically linked module set is resolved once per process. The GUI
        // preview resolves the same ids through the same entry point, so a job cannot
        // mean different things in the editor and in batch.
        const ModuleRegistry moduleRegistry = createBuiltinModuleRegistry();
        const std::string command = argv[1];
        std::optional<int> requestedFrame;
        std::filesystem::path outputOverride;
        for (int index = 3; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--output") {
                if (!outputOverride.empty() || index + 1 >= argc) {
                    throw std::invalid_argument("--output requires exactly one path");
                }
                outputOverride = std::filesystem::absolute(
                    std::filesystem::u8path(argv[++index])).lexically_normal();
            } else if (command == "render-frame" && !requestedFrame.has_value()) {
                requestedFrame = parseFrame(argv[index]);
            } else {
                throw std::invalid_argument("Unexpected argument: " + argument);
            }
        }
        RenderJob job;
        std::string error;
        if (!loadRenderJob(argv[2], job, error)) {
            std::cerr << "Render Job invalid: " << error << '\n';
            return 65;
        }
        if (!outputOverride.empty()) {
            if (command != "render-frame" && command != "render-sequence") {
                throw std::invalid_argument("--output is only valid for render-frame/render-sequence");
            }
            RenderJob overridden = job;
            if (command == "render-frame") {
                const int frame = requestedFrame.value_or(job.startFrame);
                if (frame < job.startFrame || frame > job.endFrame) {
                    throw std::invalid_argument("Requested frame is outside the Render Job frame range");
                }
                overridden.startFrame = frame;
                overridden.endFrame = frame;
            }
            if (!applyRenderJobOutputOverride(overridden, outputOverride, error)) {
                throw std::invalid_argument("Output override invalid: " + error);
            }
            job.outputStemPattern = overridden.outputStemPattern;
        }
        if (command == "validate") {
            if (requestedFrame.has_value()) {
                throw std::invalid_argument("validate does not accept a frame argument");
            }
            if (!validateRenderJobAssets(job, error, &moduleRegistry)) {
                std::cerr << "Render Job asset validation failed: " << error << '\n';
                return 66;
            }
            std::cout << "Valid Render Job schema " << job.schemaVersion << "\n"
                      << "Scene: " << job.scenePath.string() << "\n"
                      << "Frames: " << job.startFrame << '-' << job.endFrame
                      << " @ " << job.framesPerSecond << " FPS\n";
            return 0;
        }
        if (command == "simulate" || command == "bake") {
            if (requestedFrame.has_value()) {
                throw std::invalid_argument(command + " does not accept a frame argument");
            }
            if (job.module.id.empty()) {
                std::cerr << command << " requires a Render Job with a 'module' section.\n";
                return 65;
            }
            if (command == "bake" && job.simulationCache.empty()) {
                std::cerr << "bake requires output.simulationCache to name the cache file.\n";
                return 65;
            }
            if (!validateRenderJobAssets(job, error, &moduleRegistry)) {
                std::cerr << "Render Job asset validation failed: " << error << '\n';
                return 66;
            }
            SimulationCache cache;
            if (!bakeJobSimulation(job, &moduleRegistry, cache, error)) {
                std::cerr << command << " failed: " << error << '\n';
                return 70;
            }
            std::cout << "Module: " << cache.key.moduleId
                      << " | API " << cache.key.moduleApiVersion
                      << " | build " << cache.key.buildId << '\n'
                      << "Frames: " << cache.key.startFrame << '-' << cache.key.endFrame
                      << " @ " << cache.key.framesPerSecond << " FPS | seed "
                      << cache.key.seed << '\n'
                      << "Scene content hash: " << cache.key.sceneContentHash << '\n';
            for (const SimulationCacheFrame& frame : cache.frames) {
                std::cout << "Frame " << frame.frame << " content " << frame.contentHash << '\n';
            }
            if (command == "simulate") {
                std::cout << "simulate ran the module and wrote no output; "
                             "use bake to store the cache.\n";
                return 0;
            }
            if (!saveSimulationCache(job.simulationCache, cache, error)) {
                std::cerr << "Cannot write the simulation cache: " << error << '\n';
                return 74;
            }
            std::cout << "Baked " << cache.frames.size() << " frame(s) to "
                      << job.simulationCache.string() << '\n';
            return 0;
        }
        std::signal(SIGINT, handleInterrupt);
        if (command == "render-frame") {
            const int frame = requestedFrame.value_or(job.startFrame);
            BatchFrameResult result;
            if (!runRenderJobFrame(job, frame, result, error, &cancellationToken, &moduleRegistry)) {
                if (result.status == BatchFrameStatus::Cancelled) {
                    std::cerr << "Frame " << frame << " cancelled; no final output was committed.\n";
                    return 130;
                }
                std::cerr << "Frame " << frame << " failed: " << error << '\n';
                return 70;
            }
            for (const BatchOutputDiagnostic& diagnostic : result.outputDiagnostics) {
                std::cout << "Output recovery ["
                          << batchOutputDiagnosticCodeName(diagnostic.code) << '/'
                          << batchOutputRecoveryActionName(diagnostic.action) << "]: "
                          << diagnostic.message << '\n';
            }
            std::cout << "Frame " << frame << (result.skipped ? " resumed/skipped" : " complete")
                      << " | outputs " << result.outputs.size();
            if (result.cacheStatus != SimulationCacheStatus::Disabled) {
                std::cout << " | simulation cache " << simulationCacheStatusName(result.cacheStatus);
                if (!result.cacheMessage.empty()) std::cout << " (" << result.cacheMessage << ')';
            }
            std::cout << '\n';
            return 0;
        }
        if (command == "render-sequence") {
            if (requestedFrame.has_value()) {
                throw std::invalid_argument("render-sequence does not accept a frame argument");
            }
            const BatchSequenceResult result = runRenderJobSequence(
                job,
                &cancellationToken,
                [](const BatchSequenceProgress& progress) {
                    if (progress.frameStatus == BatchFrameStatus::Complete) {
                        std::cout << "Frame " << progress.frame << " complete";
                        if (!progress.message.empty()) std::cout << " | " << progress.message;
                        std::cout << '\n';
                    } else if (progress.frameStatus == BatchFrameStatus::Skipped) {
                        std::cout << "Frame " << progress.frame << " resumed/skipped\n";
                    } else if (progress.frameStatus == BatchFrameStatus::Failed) {
                        std::cerr << "Frame " << progress.frame << " failed: "
                                  << progress.message << '\n';
                    }
                },
                &moduleRegistry
            );
            if (result.status == BatchFrameStatus::Cancelled) {
                std::cerr << "Frame " << result.lastFrame
                          << " cancelled; no final output was committed.\n";
                return 130;
            }
            return result.status == BatchFrameStatus::Complete ? 0 : 70;
        }
        usage();
        return 64;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 64;
    }
}
