#include "runtime/BatchRuntime.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <glm/trigonometric.hpp>

#include "asset/BuiltinModels.h"
#include "io/AssimpImporter.h"
#include "io/ObjLoader.h"
#include "pathtracer/SceneSnapshotCapture.h"
#include "render/Camera.h"
#include "scene/SceneDocument.h"

#define RAPIDJSON_NAMESPACE myrenderer_batch_runtime_json
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#undef RAPIDJSON_NAMESPACE
#undef RAPIDJSON_NAMESPACE_BEGIN
#undef RAPIDJSON_NAMESPACE_END

namespace batch_json = myrenderer_batch_runtime_json;

namespace {

std::shared_ptr<const ModelData> loadModelData(const std::string& resource,
                                              const std::filesystem::path& scenePath) {
    if (resource == builtinGroundResource) {
        return std::make_shared<const ModelData>(makeGroundPlaneData());
    }
    if (resource == builtinGlassBackdropResource) {
        return std::make_shared<const ModelData>(makeGlassCheckerboardData());
    }
    const std::filesystem::path path = resolveSceneResource(resource, scenePath);
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("Scene model does not exist: " + path.string());
    }
    ObjLoader obj;
    AssimpImporter assimp;
    const ModelImporter* importer = obj.supports(path)
        ? static_cast<const ModelImporter*>(&obj)
        : (assimp.supports(path) ? static_cast<const ModelImporter*>(&assimp) : nullptr);
    if (importer == nullptr) throw std::runtime_error("No importer supports: " + path.string());
    ModelImportResult imported = importer->load(path);
    if (imported.model.meshes.empty()) throw std::runtime_error("Imported model has no meshes: " + path.string());
    return std::make_shared<const ModelData>(std::move(imported.model));
}

pathtracer::SceneSnapshot loadSnapshot(const RenderJob& job, const SceneDocument& document) {
    Camera camera;
    camera.setOrbitState(document.camera);
    pathtracer::SnapshotCamera snapshotCamera;
    snapshotCamera.position = camera.position();
    snapshotCamera.view = camera.viewMatrix();
    snapshotCamera.aspectRatio = static_cast<float>(job.renderSettings.width)
        / static_cast<float>(job.renderSettings.height);
    snapshotCamera.projection = camera.projectionMatrix(snapshotCamera.aspectRatio);
    snapshotCamera.verticalFieldOfViewRadians = glm::radians(camera.fieldOfView());
    pathtracer::SceneSnapshotBuilder builder(
        snapshotCamera,
        pathtracer::captureSceneLighting(document.renderer, snapshotCamera.position.y)
    );

    std::unordered_map<SceneEntityId, std::size_t> indices;
    for (std::size_t index = 0U; index < document.entities.size(); ++index) {
        indices.emplace(document.entities[index].id, index);
    }
    std::vector<glm::mat4> world(document.entities.size(), glm::mat4(1.0f));
    std::vector<unsigned char> state(document.entities.size(), 0U);
    const auto resolveWorld = [&](auto&& self, std::size_t index) -> glm::mat4 {
        if (state[index] == 2U) return world[index];
        if (state[index] == 1U) throw std::runtime_error("Scene hierarchy contains a cycle");
        state[index] = 1U;
        const SceneDocumentEntity& entity = document.entities[index];
        world[index] = entity.transform.matrix();
        if (entity.parent != invalidSceneEntityId) {
            const auto parent = indices.find(entity.parent);
            if (parent == indices.end()) throw std::runtime_error("Scene entity references a missing parent");
            world[index] = self(self, parent->second) * world[index];
        }
        state[index] = 2U;
        return world[index];
    };

    std::unordered_map<std::string, std::shared_ptr<const ModelData>> models;
    for (std::size_t index = 0U; index < document.entities.size(); ++index) {
        const SceneDocumentEntity& entity = document.entities[index];
        const glm::mat4 entityWorld = resolveWorld(resolveWorld, index);
        if (!entity.visible || entity.modelResource.empty()) continue;
        auto [entry, inserted] = models.emplace(entity.modelResource, nullptr);
        if (inserted) entry->second = loadModelData(entity.modelResource, job.scenePath);
        builder.addModel(entry->second, entity.id, entity.name, entityWorld, entity.tint,
                         entity.castsShadow);
    }
    return builder.finish();
}

// The module run that produced a frame, plus how its cache entry was treated.
struct ModuleApplication {
    ModuleRunReport report;
    SimulationCacheStatus cacheStatus{SimulationCacheStatus::Disabled};
    std::string cacheMessage;
};

// Builds the module-side view of a scene document. Entities carry their resource path but no
// GPU model, which keeps the module framework independent of the render backend.
Scene sceneForModule(const SceneDocument& document) {
    Scene scene;
    for (const SceneDocumentEntity& entity : document.entities) {
        scene.createEntityWithId(entity.id, entity.name, nullptr, entity.modelResource);
    }
    for (const SceneDocumentEntity& entity : document.entities) {
        SceneEntity* target = scene.find(entity.id);
        if (target == nullptr) continue;
        target->parent = entity.parent;
        target->transform = entity.transform;
        target->tint = entity.tint;
        target->visible = entity.visible;
        target->castsShadow = entity.castsShadow;
        target->instanceCandidate = entity.instanceCandidate;
    }
    scene.updateWorldTransforms();
    return scene;
}

// Copies the module's local transforms and tints back into the per-frame document, so
// the existing snapshot path renders the simulated frame. Nothing is written to disk
// and the editor's own scene is not involved.
void applyModuleResult(const Scene& scene, SceneDocument& document) {
    for (SceneDocumentEntity& entity : document.entities) {
        const SceneEntity* source = scene.find(entity.id);
        if (source == nullptr) continue;
        entity.transform = source->transform;
        entity.tint = source->tint;
    }
}

void applyModulePresentation(const ModuleRuntime& runtime, SceneDocument& document) {
    CameraOrbitState camera;
    RendererSettings renderer;
    runtime.applyPresentation(document.camera, document.renderer, camera, renderer);
    document.camera = camera;
    document.renderer = renderer;
}

// Applies the job's module to `document` for `frame`.
//
// With `job.simulationCache` set, a matching baked frame is reused instead of stepping
// the module: the cached entities are copied in and then *verified* by re-hashing the
// result. A cache whose key differs is never reused silently — the frame is simulated
// again and the mismatch is reported with the exact input that changed.
bool applyJobModule(
    const RenderJob& job,
    SceneDocument& document,
    int frame,
    const ModuleRegistry* modules,
    ModuleApplication& applied,
    std::string& error
) {
    if (job.module.id.empty()) return true;
    if (modules == nullptr) {
        error = "Render Job requires module '" + job.module.id
            + "' but no module registry is available";
        return false;
    }
    ModuleRuntime runtime(*modules);
    if (!runtime.configure(job.module.id, job.module.parameters, job.module.seed, error)) {
        return false;
    }
    const Scene scene = sceneForModule(document);

    // The cache key needs the authored scene hash, which a faithful runtime copy
    // reproduces exactly, so it is available before any module work happens. The key is
    // always built by the runtime itself: assembling it here once let a newly added key
    // field (the parameter fingerprint) default to zero and silently mismatched every
    // baked entry.
    if (!job.simulationCache.empty()) {
        SimulationCache cache;
        std::string cacheError;
        SimulationCacheStatus status = SimulationCacheStatus::Missing;
        std::string message = "No simulation cache at " + job.simulationCache.string();
        if (std::filesystem::is_regular_file(job.simulationCache)) {
            if (!loadSimulationCache(job.simulationCache, cache, cacheError)) {
                status = SimulationCacheStatus::Stale;
                message = "Simulation cache is unusable: " + cacheError;
            } else {
                runtime.configureTimeline(job.startFrame, job.endFrame, job.framesPerSecond);
                const SimulationCacheKey requested = runtime.cacheKey(sceneContentHash(scene));
                status = classifySimulationCache(cache, requested, message);
            }
        }
        if (status == SimulationCacheStatus::Hit) {
            const SimulationCacheFrame* cached = cache.find(frame);
            if (cached == nullptr) {
                status = SimulationCacheStatus::Stale;
                message = "Simulation cache has no frame " + std::to_string(frame);
            } else if (!runtime.reset(scene, job.startFrame, job.endFrame, job.framesPerSecond, error)) {
                return false;
            } else if (runtime.applyCachedFrame(*cached, cacheError)) {
                applied.report = runtime.report();
                applied.cacheStatus = SimulationCacheStatus::Hit;
                applied.cacheMessage = message;
                applyModuleResult(runtime.runtimeScene().scene(), document);
                applyModulePresentation(runtime, document);
                return true;
            } else {
                status = SimulationCacheStatus::Stale;
                message = "Simulation cache frame rejected: " + cacheError;
            }
        }
        applied.cacheStatus = status;
        applied.cacheMessage = message;
    }

    if (!runtime.reset(scene, job.startFrame, job.endFrame, job.framesPerSecond, error)) {
        return false;
    }
    if (!runtime.runToFrame(frame, error)) return false;
    applied.report = runtime.report();
    applyModuleResult(runtime.runtimeScene().scene(), document);
    applyModulePresentation(runtime, document);
    return true;
}



} // namespace

// Bakes the job's module over its whole frame range into `job.simulationCache`.
bool bakeJobSimulation(
    const RenderJob& job,
    const ModuleRegistry* modules,
    SimulationCache& cache,
    std::string& error
) {
    if (job.module.id.empty()) {
        error = "This Render Job does not drive a module; nothing to bake";
        return false;
    }
    if (modules == nullptr) {
        error = "Render Job requires module '" + job.module.id
            + "' but no module registry is available";
        return false;
    }
    SceneDocument document;
    if (!loadSceneDocument(job.scenePath, document, error)) return false;
    ModuleRuntime runtime(*modules);
    if (!runtime.configure(job.module.id, job.module.parameters, job.module.seed, error)) {
        return false;
    }
    const Scene scene = sceneForModule(document);
    return runtime.bakeSimulation(
        scene, job.startFrame, job.endFrame, job.framesPerSecond, cache, error
    );
}

std::filesystem::path outputStemFor(const std::filesystem::path& base,
                                    pathtracer::RenderOutput output) {
    if (output == pathtracer::RenderOutput::Beauty) return base;
    return std::filesystem::path(base.string() + "-" + renderOutputName(output));
}

constexpr std::array<pathtracer::RenderOutput, 8> allRenderOutputs{
    pathtracer::RenderOutput::Beauty,
    pathtracer::RenderOutput::Albedo,
    pathtracer::RenderOutput::Normal,
    pathtracer::RenderOutput::Depth,
    pathtracer::RenderOutput::Direct,
    pathtracer::RenderOutput::Indirect,
    pathtracer::RenderOutput::SampleCount,
    pathtracer::RenderOutput::Variance
};

constexpr std::array<pathtracer::RenderFileFormat, 3> allRenderFileFormats{
    pathtracer::RenderFileFormat::Png,
    pathtracer::RenderFileFormat::RadianceHdr,
    pathtracer::RenderFileFormat::OpenExr
};

std::set<std::string> requestedOutputNames(const RenderJob& job) {
    std::set<std::string> names;
    for (pathtracer::RenderOutput output : job.outputs) names.emplace(renderOutputName(output));
    return names;
}

std::set<std::string> requestedFormatNames(const RenderJob& job) {
    std::set<std::string> names;
    for (pathtracer::RenderFileFormat format : job.outputFormats) {
        names.emplace(renderFileFormatName(format));
    }
    return names;
}

bool readStringSet(const batch_json::Value& root, const char* key,
                   std::set<std::string>& values) {
    if (!root.HasMember(key) || !root[key].IsArray()) return false;
    for (const auto& value : root[key].GetArray()) {
        if (!value.IsString() || !values.emplace(value.GetString()).second) return false;
    }
    return !values.empty();
}

struct ReportManifestComparison {
    bool valid{false};
    bool formatsMatch{false};
    bool outputsMatch{false};
    bool settingsMatch{false};
    std::string error;
};

ReportManifestComparison compareReportManifest(const std::filesystem::path& path,
                                               const RenderJob& job, int frame) {
    ReportManifestComparison comparison;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        comparison.error = "Cannot open frame report";
        return comparison;
    }
    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    batch_json::Document root;
    root.Parse(json.c_str(), json.size());
    if (root.HasParseError()) {
        comparison.error = std::string("JSON parse error: ")
            + batch_json::GetParseError_En(root.GetParseError());
        return comparison;
    }
    if (!root.IsObject() || !root.HasMember("format") || !root["format"].IsString()
        || std::string(root["format"].GetString()) != "MyRendererFrameReport"
        || !root.HasMember("schemaVersion") || !root["schemaVersion"].IsInt()
        || root["schemaVersion"].GetInt() != 2) {
        comparison.error = "Frame report does not contain a supported schema 2 manifest";
        return comparison;
    }

    std::set<std::string> formats;
    std::set<std::string> outputs;
    if (!readStringSet(root, "formats", formats) || !readStringSet(root, "aovs", outputs)) {
        comparison.error = "Frame report format/AOV manifest is invalid";
        return comparison;
    }
    comparison.valid = true;
    comparison.formatsMatch = formats == requestedFormatNames(job);
    comparison.outputsMatch = outputs == requestedOutputNames(job);
    comparison.settingsMatch = root.HasMember("scene") && root["scene"].IsString()
        && std::string(root["scene"].GetString()) == job.scenePath.generic_u8string()
        && root.HasMember("renderer") && root["renderer"].IsString()
        && std::string(root["renderer"].GetString()) == job.renderer
        && root.HasMember("frame") && root["frame"].IsInt()
        && root["frame"].GetInt() == frame
        && root.HasMember("fps") && root["fps"].IsInt()
        && root["fps"].GetInt() == job.framesPerSecond
        && root.HasMember("seed") && root["seed"].IsUint()
        && root["seed"].GetUint() == job.renderSettings.seed
        && root.HasMember("resolution") && root["resolution"].IsArray()
        && root["resolution"].Size() == 2U
        && root["resolution"][0].IsUint() && root["resolution"][1].IsUint()
        && root["resolution"][0].GetUint() == job.renderSettings.width
        && root["resolution"][1].GetUint() == job.renderSettings.height
        && root.HasMember("spp") && root["spp"].IsUint()
        && root["spp"].GetUint() == job.renderSettings.samplesPerPixel
        && root.HasMember("maxDepth") && root["maxDepth"].IsUint()
        && root["maxDepth"].GetUint() == job.renderSettings.maxDepth
        // The module manifest is part of the frame identity: a changed module id, API
        // version, build id or seed must invalidate a resumed frame.
        && root.HasMember("module") && root["module"].IsObject()
        && root["module"].HasMember("id") && root["module"]["id"].IsString()
        && std::string(root["module"]["id"].GetString()) == job.module.id
        && (!job.module.id.empty()
            ? root["module"].HasMember("apiVersion") && root["module"]["apiVersion"].IsInt()
                && root["module"]["apiVersion"].GetInt() == moduleApiVersion
                && root["module"].HasMember("buildId") && root["module"]["buildId"].IsString()
                && !std::string(root["module"]["buildId"].GetString()).empty()
                && root["module"].HasMember("seed") && root["module"]["seed"].IsUint()
                && root["module"]["seed"].GetUint() == job.module.seed
            : true);
    // A frame produced by reusing a cache entry is only valid while the cache is still
    // the one that produced it, so the recorded status must match a fresh lookup.
    comparison.settingsMatch = comparison.settingsMatch
        && root.HasMember("simulationCache") && root["simulationCache"].IsObject();
    return comparison;
}

struct OutputSetInspection {
    bool empty{true};
    bool complete{false};
    std::vector<std::filesystem::path> managedArtifacts;
    std::vector<BatchOutputDiagnostic> diagnostics;
};

void appendDiagnostic(OutputSetInspection& inspection, int frame,
                      BatchOutputDiagnosticCode code,
                      std::vector<std::filesystem::path> paths,
                      std::string message) {
    inspection.diagnostics.push_back(BatchOutputDiagnostic{
        frame,
        code,
        BatchOutputRecoveryAction::RefuseOverwrite,
        std::move(paths),
        std::move(message)
    });
}

OutputSetInspection inspectOutputSet(const RenderJob& job, int frame,
                                     const std::filesystem::path& base) {
    OutputSetInspection inspection;
    std::set<std::filesystem::path> expectedFinals;
    std::vector<std::filesystem::path> missingExpected;
    std::vector<std::filesystem::path> stalePartials;
    std::vector<std::filesystem::path> unexpectedFinals;

    for (pathtracer::RenderOutput output : job.outputs) {
        const std::filesystem::path stem = outputStemFor(base, output);
        for (pathtracer::RenderFileFormat format : job.outputFormats) {
            expectedFinals.emplace(stem.string() + renderFileFormatExtension(format));
        }
    }

    for (pathtracer::RenderOutput output : allRenderOutputs) {
        const std::filesystem::path stem = outputStemFor(base, output);
        const std::filesystem::path temporaryStem(stem.string() + ".partial");
        for (pathtracer::RenderFileFormat format : allRenderFileFormats) {
            const char* extension = renderFileFormatExtension(format);
            const std::filesystem::path finalPath(stem.string() + extension);
            const std::filesystem::path temporaryPath(temporaryStem.string() + extension);
            if (std::filesystem::exists(finalPath)) {
                inspection.empty = false;
                inspection.managedArtifacts.push_back(finalPath);
                if (expectedFinals.find(finalPath) == expectedFinals.end()) {
                    unexpectedFinals.push_back(finalPath);
                }
            }
            if (std::filesystem::exists(temporaryPath)) {
                inspection.empty = false;
                inspection.managedArtifacts.push_back(temporaryPath);
                stalePartials.push_back(temporaryPath);
            }
        }
    }

    for (const auto& expected : expectedFinals) {
        if (!std::filesystem::is_regular_file(expected)) missingExpected.push_back(expected);
    }

    const std::filesystem::path report(base.string() + "-report.json");
    const std::filesystem::path partialReport(report.string() + ".partial");
    const bool reportExists = std::filesystem::exists(report);
    const bool reportPresent = std::filesystem::is_regular_file(report);
    if (reportExists) {
        inspection.empty = false;
        inspection.managedArtifacts.push_back(report);
    }
    if (std::filesystem::exists(partialReport)) {
        inspection.empty = false;
        inspection.managedArtifacts.push_back(partialReport);
        stalePartials.push_back(partialReport);
    }

    ReportManifestComparison manifest;
    if (reportPresent) manifest = compareReportManifest(report, job, frame);
    inspection.complete = missingExpected.empty() && reportPresent
        && stalePartials.empty() && unexpectedFinals.empty()
        && manifest.valid && manifest.formatsMatch && manifest.outputsMatch
        && manifest.settingsMatch;
    if (inspection.empty || inspection.complete) return inspection;

    if (!stalePartials.empty()) {
        appendDiagnostic(inspection, frame, BatchOutputDiagnosticCode::StalePartial,
                         stalePartials,
                         "Stale .partial artifacts indicate an interrupted encode or commit");
    }
    if (!reportPresent && missingExpected.empty()) {
        appendDiagnostic(inspection, frame, BatchOutputDiagnosticCode::MissingReport,
                         {report},
                         "All requested images exist but the frame report is missing");
    }
    if (!missingExpected.empty()
        && (inspection.managedArtifacts.size() > stalePartials.size())) {
        appendDiagnostic(inspection, frame, BatchOutputDiagnosticCode::IncompleteCommit,
                         missingExpected,
                         "The frame output set is incomplete, consistent with an interrupted commit");
    }
    if (reportPresent && !manifest.valid) {
        appendDiagnostic(inspection, frame, BatchOutputDiagnosticCode::InvalidReport,
                         {report}, "Frame report is invalid: " + manifest.error);
    } else if (reportPresent) {
        if (!manifest.formatsMatch) {
            appendDiagnostic(inspection, frame, BatchOutputDiagnosticCode::FormatSetMismatch,
                             {report},
                             "Frame report formats do not match the requested format set");
        }
        if (!manifest.outputsMatch) {
            appendDiagnostic(inspection, frame, BatchOutputDiagnosticCode::AovSetMismatch,
                             {report},
                             "Frame report AOVs do not match the requested AOV set");
        }
        if (!manifest.settingsMatch) {
            appendDiagnostic(inspection, frame, BatchOutputDiagnosticCode::ManifestMismatch,
                             {report},
                             "Frame report render settings do not match the current Render Job");
        }
    }
    if (!unexpectedFinals.empty()) {
        appendDiagnostic(inspection, frame,
                         BatchOutputDiagnosticCode::UnexpectedManagedArtifact,
                         unexpectedFinals,
                         "Managed frame artifacts exist outside the requested format/AOV set");
    }
    if (inspection.diagnostics.empty()) {
        appendDiagnostic(inspection, frame, BatchOutputDiagnosticCode::IncompleteCommit,
                         inspection.managedArtifacts,
                         "The frame output set is not a complete match for the current Render Job");
    }
    return inspection;
}

std::string summarizeDiagnostics(const std::vector<BatchOutputDiagnostic>& diagnostics) {
    std::ostringstream summary;
    for (std::size_t index = 0U; index < diagnostics.size(); ++index) {
        if (index != 0U) summary << "; ";
        summary << '[' << batchOutputDiagnosticCodeName(diagnostics[index].code) << '/'
                << batchOutputRecoveryActionName(diagnostics[index].action) << "] "
                << diagnostics[index].message;
        if (!diagnostics[index].paths.empty()) {
            summary << " (paths: ";
            for (std::size_t pathIndex = 0U;
                 pathIndex < diagnostics[index].paths.size(); ++pathIndex) {
                if (pathIndex != 0U) summary << ", ";
                summary << diagnostics[index].paths[pathIndex].string();
            }
            summary << ')';
        }
    }
    return summary.str();
}

void cleanManagedArtifacts(const std::vector<std::filesystem::path>& paths) {
    for (const auto& path : paths) {
        const std::filesystem::file_status status = std::filesystem::symlink_status(path);
        if (!std::filesystem::exists(status)) continue;
        if (!std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status)) {
            throw std::runtime_error("Refusing to remove non-file managed artifact: "
                + path.string());
        }
    }
    for (const auto& path : paths) {
        const std::filesystem::file_status status = std::filesystem::symlink_status(path);
        if (!std::filesystem::exists(status)) continue;
        std::error_code removeError;
        if (!std::filesystem::remove(path, removeError) || removeError) {
            throw std::runtime_error("Cannot remove stale managed artifact: " + path.string()
                + (removeError ? " (" + removeError.message() + ")" : ""));
        }
    }
}

void atomicWriteOutput(const pathtracer::RenderImage& image,
                       pathtracer::RenderOutput output,
                       const std::filesystem::path& finalStem,
                       const std::vector<pathtracer::RenderFileFormat>& formats) {
    if (!finalStem.parent_path().empty()) std::filesystem::create_directories(finalStem.parent_path());
    const std::filesystem::path temporaryStem(finalStem.string() + ".partial");
    std::vector<std::filesystem::path> temporaryPaths;
    std::vector<std::filesystem::path> finalPaths;
    temporaryPaths.reserve(formats.size());
    finalPaths.reserve(formats.size());
    for (pathtracer::RenderFileFormat format : formats) {
        const char* extension = renderFileFormatExtension(format);
        temporaryPaths.emplace_back(temporaryStem.string() + extension);
        finalPaths.emplace_back(finalStem.string() + extension);
    }
    std::error_code ignored;
    for (const auto& temporary : temporaryPaths) std::filesystem::remove(temporary, ignored);
    std::size_t committed = 0U;
    try {
        pathtracer::writeRenderOutput(image, output, temporaryStem, formats);
        for (; committed < formats.size(); ++committed) {
            std::filesystem::rename(temporaryPaths[committed], finalPaths[committed]);
        }
    } catch (...) {
        for (std::size_t index = 0U; index < committed; ++index) {
            std::filesystem::remove(finalPaths[index], ignored);
        }
        for (const auto& temporary : temporaryPaths) std::filesystem::remove(temporary, ignored);
        throw;
    }
}

void atomicWriteReport(const RenderJob& job, const BatchFrameResult& result,
                       const std::filesystem::path& base) {
    const std::filesystem::path finalPath(base.string() + "-report.json");
    const std::filesystem::path temporaryPath(finalPath.string() + ".partial");
    if (!finalPath.parent_path().empty()) std::filesystem::create_directories(finalPath.parent_path());
    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot write Render Job report: " + temporaryPath.string());
    output << "{\n"
           << "  \"format\": \"MyRendererFrameReport\",\n"
           << "  \"schemaVersion\": 2,\n"
           << "  \"job\": \"" << job.sourcePath.generic_u8string() << "\",\n"
           << "  \"scene\": \"" << job.scenePath.generic_u8string() << "\",\n"
           << "  \"renderer\": \"" << job.renderer << "\",\n"
           << "  \"frame\": " << result.frame << ",\n"
           << "  \"timeSeconds\": "
           << static_cast<double>(result.frame) / static_cast<double>(job.framesPerSecond) << ",\n"
           << "  \"fps\": " << job.framesPerSecond << ",\n"
           << "  \"seed\": " << job.renderSettings.seed << ",\n"
           << "  \"resolution\": [" << job.renderSettings.width << ", "
           << job.renderSettings.height << "],\n"
           << "  \"spp\": " << job.renderSettings.samplesPerPixel << ",\n"
           << "  \"maxDepth\": " << job.renderSettings.maxDepth << ",\n"
           << "  \"aovs\": [";
    for (std::size_t index = 0U; index < job.outputs.size(); ++index) {
        if (index != 0U) output << ", ";
        output << "\"" << renderOutputName(job.outputs[index]) << "\"";
    }
    output << "],\n"
           << "  \"formats\": [";
    for (std::size_t index = 0U; index < job.outputFormats.size(); ++index) {
        if (index != 0U) output << ", ";
        output << "\"" << renderFileFormatName(job.outputFormats[index]) << "\"";
    }
    output << "],\n"
           << "  \"colorSpace\": \"PNG: reinhard-sRGB; HDR: linear-RGBE; EXR: linear-RGB-float32\",\n"
           << "  \"renderMilliseconds\": " << result.statistics.renderMilliseconds << ",\n"
           << "  \"module\": {"
           << "\"id\": \"" << result.module.moduleId << "\", "
           << "\"apiVersion\": " << result.module.apiVersion << ", "
           << "\"buildId\": \"" << result.module.buildId << "\", "
           << "\"seed\": " << result.module.seed << ", "
           << "\"lastFrame\": " << result.module.lastFrame << ", "
           << "\"inputHash\": " << result.module.inputContentHash << ", "
           << "\"contentHash\": " << result.module.contentHash << ", "
           << "\"state\": \"" << result.module.moduleState << "\"},\n"
           << "  \"simulationCache\": {\"status\": \""
           << simulationCacheStatusName(result.cacheStatus) << "\", "
           << "\"message\": \"" << result.cacheMessage << "\"},\n"
           << "  \"status\": \"Complete\"\n"
           << "}\n";
    output.close();
    if (!output) throw std::runtime_error("Failed while writing Render Job report");
    std::filesystem::rename(temporaryPath, finalPath);
}

const char* batchOutputDiagnosticCodeName(BatchOutputDiagnosticCode code) {
    switch (code) {
        case BatchOutputDiagnosticCode::StalePartial: return "StalePartial";
        case BatchOutputDiagnosticCode::MissingReport: return "MissingReport";
        case BatchOutputDiagnosticCode::IncompleteCommit: return "IncompleteCommit";
        case BatchOutputDiagnosticCode::FormatSetMismatch: return "FormatSetMismatch";
        case BatchOutputDiagnosticCode::AovSetMismatch: return "AovSetMismatch";
        case BatchOutputDiagnosticCode::InvalidReport: return "InvalidReport";
        case BatchOutputDiagnosticCode::ManifestMismatch: return "ManifestMismatch";
        case BatchOutputDiagnosticCode::UnexpectedManagedArtifact:
            return "UnexpectedManagedArtifact";
    }
    return "Unknown";
}

const char* batchOutputRecoveryActionName(BatchOutputRecoveryAction action) {
    switch (action) {
        case BatchOutputRecoveryAction::RefuseOverwrite: return "RefuseOverwrite";
        case BatchOutputRecoveryAction::CleanAndRerender: return "CleanAndRerender";
    }
    return "Unknown";
}

bool validateRenderJobAssets(const RenderJob& job, std::string& error,
                             const ModuleRegistry* modules) {
    try {
        SceneDocument document;
        std::string loadError;
        if (!loadSceneDocument(job.scenePath, document, loadError)) {
            throw std::runtime_error(loadError);
        }
        (void)loadSnapshot(job, document);
        if (!job.module.id.empty()) {
            if (modules == nullptr) {
                throw std::runtime_error(
                    "Render Job requires module '" + job.module.id
                    + "' but no module registry is available"
                );
            }
            const ModuleManifest* manifest = modules->find(job.module.id);
            if (manifest == nullptr) {
                throw std::runtime_error("Unknown Render Job module id: " + job.module.id);
            }
            // Parameter ids, kinds and ranges are only knowable once the module has
            // declared them, so validation configures a throwaway instance.
            ModuleRuntime runtime(*modules);
            if (!runtime.configure(job.module.id, job.module.parameters, job.module.seed, error)) {
                return false;
            }
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool runRenderJobFrame(const RenderJob& job, int frame, BatchFrameResult& result,
                       std::string& error, const CancellationToken* cancellation,
                       const ModuleRegistry* modules) {
    result = BatchFrameResult{};
    result.frame = frame;
    if (job.renderer != "cpu-path-traced") {
        error = "Raster Render Jobs run with Iris raster-sequence <job.renderjob>";
        result.status = BatchFrameStatus::Failed;
        return false;
    }
    if (cancellation != nullptr && cancellation->isCancellationRequested()) {
        result.status = BatchFrameStatus::Cancelled;
        error = "Render Job cancelled";
        return false;
    }
    try {
        if (frame < job.startFrame || frame > job.endFrame) {
            throw std::runtime_error("Requested frame is outside the Render Job frame range");
        }
        const std::filesystem::path base = renderJobFrameStem(job, frame);
        OutputSetInspection inspection = inspectOutputSet(job, frame, base);
        if (inspection.complete && job.resume) {
            result.skipped = true;
            result.status = BatchFrameStatus::Skipped;
            error.clear();
            return true;
        }
        if (inspection.complete && !job.resume) {
            throw std::runtime_error(
                "[CompleteOutputSet/RefuseOverwrite] Frame output set already exists; "
                "enable resume to skip verified complete frames");
        }
        if (!inspection.empty) {
            const BatchOutputRecoveryAction action = job.resume
                ? BatchOutputRecoveryAction::CleanAndRerender
                : BatchOutputRecoveryAction::RefuseOverwrite;
            for (auto& diagnostic : inspection.diagnostics) diagnostic.action = action;
            result.outputDiagnostics = inspection.diagnostics;
            if (!job.resume) throw std::runtime_error(summarizeDiagnostics(result.outputDiagnostics));
            cleanManagedArtifacts(inspection.managedArtifacts);
        }

        result.status = BatchFrameStatus::Running;
        SceneDocument document;
        std::string loadError;
        if (!loadSceneDocument(job.scenePath, document, loadError)) {
            throw std::runtime_error(loadError);
        }
        ModuleApplication moduleApplication;
        if (!applyJobModule(job, document, frame, modules, moduleApplication, error)) {
            result.status = BatchFrameStatus::Failed;
            return false;
        }
        result.module = moduleApplication.report;
        result.cacheStatus = moduleApplication.cacheStatus;
        result.cacheMessage = moduleApplication.cacheMessage;
        pathtracer::ProgressiveRenderer renderer(loadSnapshot(job, document), job.renderSettings);
        while (renderer.renderPass(cancellation == nullptr ? nullptr : cancellation->nativeFlag())) {}
        if (cancellation != nullptr && cancellation->isCancellationRequested()) {
            result.status = BatchFrameStatus::Cancelled;
            error = "Render Job cancelled";
            return false;
        }
        const pathtracer::RenderImage& image = renderer.image();
        result.statistics = image.statistics;
        for (pathtracer::RenderOutput output : job.outputs) {
            const std::filesystem::path stem = outputStemFor(base, output);
            atomicWriteOutput(image, output, stem, job.outputFormats);
            for (pathtracer::RenderFileFormat format : job.outputFormats) {
                result.outputs.push_back(stem.string() + renderFileFormatExtension(format));
            }
        }
        atomicWriteReport(job, result, base);
        result.outputs.push_back(base.string() + "-report.json");
        result.status = BatchFrameStatus::Complete;
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        result.status = BatchFrameStatus::Failed;
        error = exception.what();
        return false;
    }
}

BatchSequenceResult runRenderJobSequence(
    const RenderJob& job,
    const CancellationToken* cancellation,
    const BatchSequenceProgressCallback& progress,
    const ModuleRegistry* modules
) {
    BatchSequenceResult sequence;
    sequence.lastFrame = job.startFrame;
    sequence.totalFrames = job.endFrame - job.startFrame + 1;
    const auto publish = [&](int frame, BatchFrameStatus status, const std::string& message = {}) {
        if (!progress) return;
        progress(BatchSequenceProgress{
            frame,
            sequence.completedFrames,
            sequence.totalFrames,
            status,
            message
        });
    };
    publish(job.startFrame, BatchFrameStatus::Pending);

    for (int frame = job.startFrame; frame <= job.endFrame; ++frame) {
        sequence.lastFrame = frame;
        if (cancellation != nullptr && cancellation->isCancellationRequested()) {
            sequence.status = BatchFrameStatus::Cancelled;
            sequence.error = "Render Job cancelled";
            publish(frame, sequence.status, sequence.error);
            return sequence;
        }

        publish(frame, BatchFrameStatus::Running);
        BatchFrameResult frameResult;
        std::string frameError;
        if (!runRenderJobFrame(job, frame, frameResult, frameError, cancellation, modules)) {
            if (!frameResult.outputDiagnostics.empty()) {
                sequence.outputDiagnostics.insert(sequence.outputDiagnostics.end(),
                                                  frameResult.outputDiagnostics.begin(),
                                                  frameResult.outputDiagnostics.end());
            }
            if (frameResult.status == BatchFrameStatus::Cancelled) {
                sequence.status = BatchFrameStatus::Cancelled;
                sequence.error = frameError;
                publish(frame, sequence.status, sequence.error);
                return sequence;
            }
            ++sequence.failedFrames;
            sequence.error = frameError;
            publish(frame, BatchFrameStatus::Failed, frameError);
            if (job.failurePolicy == RenderJobFailurePolicy::Stop) {
                sequence.status = BatchFrameStatus::Failed;
                return sequence;
            }
            continue;
        }

        ++sequence.completedFrames;
        if (frameResult.status == BatchFrameStatus::Skipped) ++sequence.skippedFrames;
        if (!frameResult.outputDiagnostics.empty()) {
            ++sequence.recoveredFrames;
            sequence.outputDiagnostics.insert(sequence.outputDiagnostics.end(),
                                              frameResult.outputDiagnostics.begin(),
                                              frameResult.outputDiagnostics.end());
        }
        sequence.outputCount += frameResult.outputs.size();
        publish(frame, frameResult.status, summarizeDiagnostics(frameResult.outputDiagnostics));
    }

    sequence.status = sequence.failedFrames == 0
        ? BatchFrameStatus::Complete
        : BatchFrameStatus::Failed;
    return sequence;
}
