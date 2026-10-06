#include "module/BuiltinModules.h"
#include "module/ModuleRegistry.h"
#include "runtime/BatchRuntime.h"
#include "runtime/RenderJob.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool hasOutputDiagnostic(const BatchFrameResult& result,
                         BatchOutputDiagnosticCode code,
                         BatchOutputRecoveryAction action) {
    for (const auto& diagnostic : result.outputDiagnostics) {
        if (diagnostic.code == code && diagnostic.action == action) return true;
    }
    return false;
}
}

int main() {
    try {
        const std::filesystem::path sourceRoot(MYRENDERER_SOURCE_DIR);
        const std::filesystem::path jobPath = sourceRoot / "assets" / "renderjobs"
            / "01_cpu_reference.renderjob";
        RenderJob job;
        std::string error;
        require(loadRenderJob(jobPath, job, error), error.c_str());
        require(job.schemaVersion == 1, "Render Job schema version was not loaded");
        require(job.outputs.size() == 8U, "Render Job AOV list was not loaded");
        require(job.outputFormats.size() == 2U, "Render Job output formats were not loaded");
        require(renderJobFrameStem(job, 7).filename() == "frame_0007", "frame token expansion failed");
        require(validateRenderJobAssets(job, error), error.c_str());

        RenderJob rasterJob;
        require(loadRenderJob(sourceRoot / "assets" / "renderjobs"
                    / "04_coastal_sequence.renderjob", rasterJob, error), error.c_str());
        require(rasterJob.renderer == "raster", "coastal fixture must select the raster backend");
        const ModuleRegistry rasterModules = createBuiltinModuleRegistry();
        require(validateRenderJobAssets(rasterJob, error, &rasterModules), error.c_str());
        RenderJob implicitModule = job;
        implicitModule.scenePath = sourceRoot / "assets/scenes/fixtures/26_module_workflow.myscene";
        require(!validateRenderJobAssets(implicitModule, error, &rasterModules)
                    && error.find("explicit Render Job module") != std::string::npos,
                "Batch must diagnose a Scene module without explicit Job configuration");
        BatchFrameResult wrongBackend;
        require(!runRenderJobFrame(rasterJob, 0, wrongBackend, error),
                "CPU Batch must reject raster frames explicitly");
        require(error.find("raster-sequence") != std::string::npos,
                "raster backend rejection must name its command");
        require(!validateQueueRenderJob(rasterJob, error)
                    && error.find("raster-sequence") != std::string::npos,
                "Queue support validation must reject Raster with an actionable diagnostic");
        RenderJob unsupportedFormat = rasterJob;
        unsupportedFormat.outputFormats = {pathtracer::RenderFileFormat::OpenExr};
        require(!validateRenderJob(unsupportedFormat, error), "Raster EXR must be rejected");
        RenderJob unsupportedCache = rasterJob;
        unsupportedCache.simulationCache = "missing-cache.json";
        require(!validateRenderJob(unsupportedCache, error), "Raster cache must be rejected");
        unsupportedCache = job;
        unsupportedCache.simulationCache = "missing-cache.json";
        require(!validateRenderJob(unsupportedCache, error), "CPU cache without explicit module must be rejected");
        rasterJob.resume = true;
        require(!validateRenderJob(rasterJob, error),
                "raster resume must be rejected until output manifests are implemented");
        rasterJob.resume = false;
        rasterJob.outputs.push_back(pathtracer::RenderOutput::Depth);
        require(!validateRenderJob(rasterJob, error),
                "raster AOVs must be rejected until supported");

        const std::filesystem::path outputRoot = std::filesystem::temp_directory_path()
            / "MyRendererRenderJobAcceptance";
        std::error_code cleanupError;
        std::filesystem::remove_all(outputRoot, cleanupError);
        std::filesystem::create_directories(outputRoot);
        job.renderSettings.width = 16U;
        job.renderSettings.height = 16U;
        job.renderSettings.samplesPerPixel = 1U;
        job.startFrame = 0;
        job.endFrame = 0;
        job.outputs = {pathtracer::RenderOutput::Beauty, pathtracer::RenderOutput::Normal};
        job.outputStemPattern = outputRoot / "frame_{frame:04}";
        job.resume = true;

        const std::filesystem::path exrJobPath = outputRoot / "exr.renderjob";
        const std::filesystem::path exrStem = outputRoot / "exr" / "frame_{frame:04}";
        {
            std::ofstream exrJob(exrJobPath, std::ios::binary | std::ios::trunc);
            exrJob << "{\n"
                   << "  \"format\": \"MyRendererRenderJob\",\n"
                   << "  \"schemaVersion\": 1,\n"
                   << "  \"scene\": \"" << job.scenePath.generic_u8string() << "\",\n"
                   << "  \"renderer\": \"cpu-path-traced\",\n"
                   << "  \"camera\": \"scene\",\n"
                   << "  \"resolution\": [8, 8],\n"
                   << "  \"frames\": {\"start\": 0, \"end\": 0, \"fps\": 24},\n"
                   << "  \"sampling\": {\"spp\": 1, \"maxDepth\": 2, \"seed\": 7},\n"
                   << "  \"aovs\": [\"beauty\"],\n"
                   << "  \"output\": {\"path\": \"" << exrStem.generic_u8string()
                   << "\", \"formats\": [\"exr\"], \"resume\": true},\n"
                   << "  \"simulationCache\": \"\",\n"
                   << "  \"failurePolicy\": \"stop\"\n"
                   << "}\n";
            require(static_cast<bool>(exrJob), "failed to write OpenEXR Render Job fixture");
        }
        RenderJob exrJob;
        require(loadRenderJob(exrJobPath, exrJob, error), error.c_str());
        require(exrJob.outputFormats.size() == 1U
                && exrJob.outputFormats[0] == pathtracer::RenderFileFormat::OpenExr,
                "OpenEXR output format was not parsed");
        BatchFrameResult exrResult;
        require(runRenderJobFrame(exrJob, 0, exrResult, error), error.c_str());
        const std::filesystem::path exrPath = outputRoot / "exr" / "frame_0000.exr";
        require(std::filesystem::is_regular_file(exrPath), "OpenEXR output was not produced");
        require(!std::filesystem::exists(outputRoot / "exr" / "frame_0000.png")
                && !std::filesystem::exists(outputRoot / "exr" / "frame_0000.hdr"),
                "unrequested image formats were produced");
        require(exrResult.outputs.size() == 2U,
                "OpenEXR frame should report one image and one report");
        std::array<unsigned char, 4> exrMagic{};
        {
            std::ifstream exrFile(exrPath, std::ios::binary);
            exrFile.read(reinterpret_cast<char*>(exrMagic.data()),
                         static_cast<std::streamsize>(exrMagic.size()));
            require(static_cast<bool>(exrFile), "OpenEXR output header could not be read");
        }
        require(exrMagic == std::array<unsigned char, 4>{0x76U, 0x2fU, 0x31U, 0x01U},
                "OpenEXR output magic is invalid");
        BatchFrameResult exrResumed;
        require(runRenderJobFrame(exrJob, 0, exrResumed, error), error.c_str());
        require(exrResumed.status == BatchFrameStatus::Skipped,
                "OpenEXR-only complete frame was not resumed/skipped");

        RenderJob cancelledJob = job;
        cancelledJob.outputStemPattern = outputRoot / "cancelled" / "frame_{frame:04}";
        cancelledJob.renderSettings.samplesPerPixel = 1000000U;
        CancellationToken cancellation;
        std::thread cancelThread([&cancellation] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            cancellation.requestCancellation();
        });
        BatchFrameResult cancelled;
        const bool cancelledFrameCompleted = runRenderJobFrame(
            cancelledJob, 0, cancelled, error, &cancellation);
        cancelThread.join();
        require(!cancelledFrameCompleted, "cancelled frame unexpectedly rendered");
        require(cancelled.status == BatchFrameStatus::Cancelled,
                "cancelled frame did not expose Cancelled status");
        require(cancelled.outputs.empty(), "cancelled frame published output paths");
        require(!std::filesystem::exists(outputRoot / "cancelled"),
                "cancelled frame left output artifacts");

        BatchFrameResult first;
        require(runRenderJobFrame(job, 0, first, error), error.c_str());
        require(!first.skipped, "first frame unexpectedly skipped");
        require(first.status == BatchFrameStatus::Complete,
                "rendered frame did not expose Complete status");
        require(std::filesystem::is_regular_file(outputRoot / "frame_0000.png"),
                "beauty PNG was not produced");
        require(std::filesystem::is_regular_file(outputRoot / "frame_0000.hdr"),
                "beauty HDR was not produced");
        require(std::filesystem::is_regular_file(outputRoot / "frame_0000-normal.png"),
                "normal AOV was not produced");
        require(std::filesystem::is_regular_file(outputRoot / "frame_0000-report.json"),
                "frame report was not produced");
        {
            std::ifstream report(outputRoot / "frame_0000-report.json", std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(report)),
                                   std::istreambuf_iterator<char>());
            require(text.find("\"format\": \"MyRendererFrameReport\"") != std::string::npos
                    && text.find("\"schemaVersion\": 2") != std::string::npos
                    && text.find("\"aovs\": [\"beauty\", \"normal\"]")
                        != std::string::npos,
                    "frame report does not contain the schema 2 output manifest");
        }

        BatchFrameResult resumed;
        require(runRenderJobFrame(job, 0, resumed, error), error.c_str());
        require(resumed.skipped, "complete frame was not resumed/skipped");
        require(resumed.status == BatchFrameStatus::Skipped,
                "resumed frame did not expose Skipped status");

        std::filesystem::remove(outputRoot / "frame_0000-report.json");
        BatchFrameResult missingReportRecovered;
        require(runRenderJobFrame(job, 0, missingReportRecovered, error), error.c_str());
        require(hasOutputDiagnostic(missingReportRecovered,
                                    BatchOutputDiagnosticCode::MissingReport,
                                    BatchOutputRecoveryAction::CleanAndRerender),
                "missing report was not diagnosed and safely re-rendered");
        require(std::filesystem::is_regular_file(outputRoot / "frame_0000-report.json"),
                "missing report recovery did not recreate the report");

        std::filesystem::remove(outputRoot / "frame_0000-normal.hdr");
        BatchFrameResult interruptedCommitRecovered;
        require(runRenderJobFrame(job, 0, interruptedCommitRecovered, error), error.c_str());
        require(hasOutputDiagnostic(interruptedCommitRecovered,
                                    BatchOutputDiagnosticCode::IncompleteCommit,
                                    BatchOutputRecoveryAction::CleanAndRerender),
                "interrupted commit was not diagnosed and safely re-rendered");

        const std::filesystem::path stalePartial = outputRoot / "frame_0000.partial.png";
        {
            std::ofstream partial(stalePartial, std::ios::binary | std::ios::trunc);
            partial << "interrupted encoder";
        }
        BatchFrameResult stalePartialRecovered;
        require(runRenderJobFrame(job, 0, stalePartialRecovered, error), error.c_str());
        require(hasOutputDiagnostic(stalePartialRecovered,
                                    BatchOutputDiagnosticCode::StalePartial,
                                    BatchOutputRecoveryAction::CleanAndRerender),
                "stale partial artifact was not diagnosed and safely re-rendered");
        require(!std::filesystem::exists(stalePartial),
                "stale partial artifact survived recovery");

        RenderJob pngOnly = job;
        pngOnly.outputFormats = {pathtracer::RenderFileFormat::Png};
        BatchFrameResult formatMismatchRecovered;
        require(runRenderJobFrame(pngOnly, 0, formatMismatchRecovered, error), error.c_str());
        require(hasOutputDiagnostic(formatMismatchRecovered,
                                    BatchOutputDiagnosticCode::FormatSetMismatch,
                                    BatchOutputRecoveryAction::CleanAndRerender),
                "format-set mismatch was not diagnosed and safely re-rendered");
        require(!std::filesystem::exists(outputRoot / "frame_0000.hdr")
                && !std::filesystem::exists(outputRoot / "frame_0000-normal.hdr"),
                "format-set recovery left stale unrequested HDR outputs");

        {
            std::ofstream partial(stalePartial, std::ios::binary | std::ios::trunc);
            partial << "must be preserved";
        }
        RenderJob refuseRecovery = pngOnly;
        refuseRecovery.resume = false;
        BatchFrameResult refused;
        require(!runRenderJobFrame(refuseRecovery, 0, refused, error),
                "resume=false unexpectedly cleaned an abnormal output set");
        require(hasOutputDiagnostic(refused,
                                    BatchOutputDiagnosticCode::StalePartial,
                                    BatchOutputRecoveryAction::RefuseOverwrite),
                "resume=false did not expose the refusal diagnostic");
        require(std::filesystem::is_regular_file(stalePartial),
                "resume=false modified the abnormal output set");
        std::filesystem::remove(stalePartial);

        RenderJob sequenceJob = job;
        sequenceJob.startFrame = 0;
        sequenceJob.endFrame = 1;
        sequenceJob.outputs = {pathtracer::RenderOutput::Beauty};
        sequenceJob.outputStemPattern = outputRoot / "sequence" / "frame_{frame:04}";
        int progressEvents = 0;
        const BatchSequenceResult sequence = runRenderJobSequence(
            sequenceJob,
            nullptr,
            [&progressEvents](const BatchSequenceProgress&) { ++progressEvents; }
        );
        require(sequence.status == BatchFrameStatus::Complete,
                "shared sequence runtime did not complete");
        require(sequence.completedFrames == 2 && sequence.totalFrames == 2,
                "shared sequence runtime reported incorrect progress");
        require(sequence.outputCount == 6U,
                "shared sequence runtime reported incorrect output count");
        require(progressEvents >= 5, "shared sequence runtime did not publish progress");

        std::filesystem::remove(outputRoot / "sequence" / "frame_0000-report.json");
        const BatchSequenceResult recoveredSequence = runRenderJobSequence(sequenceJob);
        require(recoveredSequence.status == BatchFrameStatus::Complete
                && recoveredSequence.recoveredFrames == 1
                && recoveredSequence.skippedFrames == 1,
                "sequence recovery did not report one recovered and one skipped frame");
        require(!recoveredSequence.outputDiagnostics.empty()
                && recoveredSequence.outputDiagnostics.front().frame == 0,
                "sequence recovery did not propagate structured frame diagnostics");

        const std::filesystem::path originalPattern = sequenceJob.outputStemPattern;
        require(!applyRenderJobOutputOverride(
                    sequenceJob, outputRoot / "override" / "frame", error),
                "sequence output override without a frame token unexpectedly succeeded");
        require(sequenceJob.outputStemPattern == originalPattern,
                "failed output override mutated the Render Job");
        require(applyRenderJobOutputOverride(
                    sequenceJob, outputRoot / "override" / "frame_{frame:03}", error),
                error.c_str());
        require(renderJobFrameStem(sequenceJob, 7).filename() == "frame_007",
                "valid output override was not applied");

        RenderJob unchanged = job;
        const std::filesystem::path invalidPath = outputRoot / "invalid.renderjob";
        {
            std::ofstream invalid(invalidPath);
            invalid << "{not json";
        }
        require(!loadRenderJob(invalidPath, unchanged, error), "invalid JSON unexpectedly loaded");
        require(unchanged.scenePath == job.scenePath, "failed load mutated the destination job");

        // A schema 2 job drives its scene with a C++ module. The frame must be a pure
        // function of the job inputs, so two runs of the same frame agree byte for byte,
        // and a different frame of the same sequence must differ.
        const ModuleRegistry modules = createBuiltinModuleRegistry();
        const std::filesystem::path moduleJobPath = outputRoot / "module.renderjob";
        {
            std::ofstream moduleJob(moduleJobPath, std::ios::binary | std::ios::trunc);
            moduleJob << "{\n"
                      << "  \"format\": \"MyRendererRenderJob\",\n"
                      << "  \"schemaVersion\": 2,\n"
                      << "  \"scene\": \"" << job.scenePath.generic_u8string() << "\",\n"
                      << "  \"renderer\": \"cpu-path-traced\",\n"
                      << "  \"camera\": \"scene\",\n"
                      << "  \"resolution\": [16, 16],\n"
                      << "  \"frames\": {\"start\": 0, \"end\": 2, \"fps\": 24},\n"
                      << "  \"sampling\": {\"spp\": 1, \"maxDepth\": 3, \"seed\": 20260919},\n"
                      << "  \"aovs\": [\"beauty\"],\n"
                      << "  \"output\": {\"path\": \""
                      << (outputRoot / "module" / "frame_{frame:04}").generic_u8string()
                      << "\", \"formats\": [\"png\"], \"resume\": false},\n"
                      << "  \"module\": {\"id\": \"" << BuiltinModules::turntableId
                      << "\", \"seed\": 20260919,\n"
                      << "    \"parameters\": {\"degreesPerFrame\": 45.0, \"axis\": \"Y\","
                      << " \"carouselRadius\": 1.5, \"tintStrength\": 0.5}},\n"
                      << "  \"failurePolicy\": \"stop\"\n"
                      << "}\n";
        }
        RenderJob moduleJob;
        require(loadRenderJob(moduleJobPath, moduleJob, error), error.c_str());
        require(moduleJob.schemaVersion == 2, "schema 2 module job was not loaded");
        require(moduleJob.module.id == BuiltinModules::turntableId,
                "module id was not loaded");
        require(moduleJob.module.seed == 20260919U, "module seed was not loaded");
        require(moduleJob.module.parameters.size() == 4U,
                "module parameter overrides were not loaded");
        require(!validateRenderJobAssets(moduleJob, error),
                "a module job must not validate without a module registry");
        require(error.find(BuiltinModules::turntableId) != std::string::npos,
                "the missing registry must name the module");
        require(validateRenderJobAssets(moduleJob, error, &modules), error.c_str());

        RenderJob unknownModule = moduleJob;
        unknownModule.module.id = "myrenderer.core.missing";
        require(!validateRenderJobAssets(unknownModule, error, &modules),
                "an unknown module id must fail asset validation");
        RenderJob badParameter = moduleJob;
        badParameter.module.parameters.front().id = "degreesPerFrame";
        badParameter.module.parameters.front().value.type = ModuleParameterType::Asset;
        badParameter.module.parameters.front().value.text = "spinning";
        require(!validateRenderJobAssets(badParameter, error, &modules),
                "a module parameter of the wrong kind must fail asset validation");
        require(error.find("degreesPerFrame") != std::string::npos,
                "the rejected parameter must be named");

        const auto runModuleFrame = [&](int frame, const std::filesystem::path& stem) {
            RenderJob runJob = moduleJob;
            runJob.outputStemPattern = stem;
            BatchFrameResult result;
            require(runRenderJobFrame(runJob, frame, result, error, nullptr, &modules),
                    error.c_str());
            require(result.module.moduleId == BuiltinModules::turntableId,
                    "the frame result must report the module that produced it");
            require(result.module.apiVersion == moduleApiVersion,
                    "the frame result must report the module API version");
            require(!result.module.buildId.empty(), "the frame result must report a build id");
            require(result.module.seed == 20260919U, "the frame result must report its seed");
            require(result.module.lastFrame == frame, "the module run stopped on the wrong frame");
            return result;
        };
        const BatchFrameResult firstFrame = runModuleFrame(0, outputRoot / "moduleA" / "frame_{frame:04}");
        const BatchFrameResult secondFrame = runModuleFrame(2, outputRoot / "moduleB" / "frame_{frame:04}");
        require(firstFrame.module.contentHash != secondFrame.module.contentHash,
                "two animated frames must produce different runtime content");
        require(firstFrame.module.inputContentHash == secondFrame.module.inputContentHash,
                "both frames must start from the same authored scene");
        require(!firstFrame.module.moduleState.empty(),
                "the module must report its deterministic state");
        const BatchFrameResult repeatedFrame = runModuleFrame(2, outputRoot / "moduleC" / "frame_{frame:04}");
        require(repeatedFrame.module.contentHash == secondFrame.module.contentHash
                && repeatedFrame.module.moduleState == secondFrame.module.moduleState,
                "re-running a frame must reproduce the module result exactly");
        {
            const auto readBytes = [](const std::filesystem::path& path) {
                std::ifstream input(path, std::ios::binary);
                return std::string((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
            };
            const std::string second = readBytes(outputRoot / "moduleB" / "frame_0002.png");
            const std::string repeated = readBytes(outputRoot / "moduleC" / "frame_0002.png");
            require(!second.empty() && second == repeated,
                    "the same module frame must render to identical bytes");
            const std::string startFrameBytes = readBytes(outputRoot / "moduleA" / "frame_0000.png");
            require(!startFrameBytes.empty() && startFrameBytes != second,
                    "an animated module must change the rendered frame");
        }
        {
            // The report manifest carries the module identity, so a resumed frame cannot
            // silently reuse output produced by a different module or build.
            std::ifstream report(outputRoot / "moduleB" / "frame_0002-report.json", std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(report)),
                                   std::istreambuf_iterator<char>());
            require(text.find("\"id\": \"" + std::string(BuiltinModules::turntableId) + "\"")
                        != std::string::npos,
                    "the frame report must record the module id");
            require(text.find("\"buildId\"") != std::string::npos
                    && text.find("\"contentHash\"") != std::string::npos,
                    "the frame report must record the module build id and content hash");
        }
        RenderJob changedSeed = moduleJob;
        changedSeed.module.seed = 4242U;
        changedSeed.outputStemPattern = outputRoot / "moduleB" / "frame_{frame:04}";
        changedSeed.resume = true;
        BatchFrameResult resumedFrame;
        require(runRenderJobFrame(changedSeed, 2, resumedFrame, error, nullptr, &modules),
                error.c_str());
        require(!resumedFrame.skipped,
                "a different module seed must not reuse a resumed frame");

        // A baked simulation cache must reproduce the simulated frames exactly, and a
        // cache built from other inputs or tampered with must be reported, not reused.
        {
            RenderJob cachedJob = moduleJob;
            const std::filesystem::path cachePath = outputRoot / "simulation-cache.json";
            cachedJob.simulationCache = cachePath;
            cachedJob.outputStemPattern = outputRoot / "cached" / "frame_{frame:04}";
            SimulationCache cache;
            require(bakeJobSimulation(cachedJob, &modules, cache, error), error.c_str());
            require(cache.frames.size() == 3U, "a bake must cover the whole job frame range");
            require(saveSimulationCache(cachePath, cache, error), error.c_str());

            BatchFrameResult cachedFrame;
            require(runRenderJobFrame(cachedJob, 2, cachedFrame, error, nullptr, &modules),
                    error.c_str());
            require(cachedFrame.cacheStatus == SimulationCacheStatus::Hit,
                    "a matching cache must be reused");
            require(cachedFrame.module.contentHash == secondFrame.module.contentHash,
                    "a reused cache must reproduce the simulated content hash");
            {
                const auto readBytes = [](const std::filesystem::path& path) {
                    std::ifstream input(path, std::ios::binary);
                    return std::string((std::istreambuf_iterator<char>(input)),
                                       std::istreambuf_iterator<char>());
                };
                require(readBytes(outputRoot / "cached" / "frame_0002.png")
                            == readBytes(outputRoot / "moduleB" / "frame_0002.png"),
                        "a cached frame must render to the same bytes as the simulated frame");
            }

            RenderJob staleJob = cachedJob;
            staleJob.module.seed = 4242U;
            staleJob.outputStemPattern = outputRoot / "stale" / "frame_{frame:04}";
            BatchFrameResult staleFrame;
            require(runRenderJobFrame(staleJob, 2, staleFrame, error, nullptr, &modules),
                    error.c_str());
            require(staleFrame.cacheStatus == SimulationCacheStatus::Stale,
                    "a cache built from other inputs must be reported stale");
            require(staleFrame.cacheMessage.find("seed") != std::string::npos,
                    "the stale report must name the changed input");

            SimulationCache tampered = cache;
            require(!tampered.frames.front().entities.empty(), "a baked frame must carry entities");
            tampered.frames.front().entities.front().transform.translation.x += 3.0f;
            require(saveSimulationCache(cachePath, tampered, error), error.c_str());
            BatchFrameResult recoveredFrame;
            require(runRenderJobFrame(cachedJob, 0, recoveredFrame, error, nullptr, &modules),
                    error.c_str());
            require(recoveredFrame.cacheStatus == SimulationCacheStatus::Stale,
                    "a tampered cache must not be reused");
            require(recoveredFrame.module.contentHash == firstFrame.module.contentHash,
                    "a rejected cache must fall back to simulating the frame");
        }
        std::filesystem::remove_all(outputRoot, cleanupError);
        std::cout << "Render Job schema, output recovery diagnostics, cancellation, atomic output, and resume tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Render Job tests failed: " << exception.what() << '\n';
        return 1;
    }
}
