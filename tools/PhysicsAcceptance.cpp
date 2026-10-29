#include "module/BuiltinModules.h"
#include "runtime/BatchRuntime.h"
#include "scene/SceneDocument.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
Scene authoredScene(const SceneDocument& document, const std::filesystem::path& scenePath) {
    Scene result;
    for (const auto& input : document.entities) {
        // Match the editor's loaded-scene resource identity, rather than copying
        // document-relative spellings into the runtime hash.
        const auto resource = input.modelResource.empty() || input.modelResource.rfind("builtin:", 0) == 0
            ? input.modelResource : resolveSceneResource(input.modelResource, scenePath).generic_u8string();
        auto* entity = result.find(result.createEntityWithId(input.id, input.name, nullptr, resource));
        entity->parent = input.parent; entity->transform = input.transform;
        entity->rigidBody = input.rigidBody; entity->light = input.light;
        entity->tint = input.tint; entity->visible = input.visible; entity->castsShadow = input.castsShadow;
        entity->instanceCandidate = input.instanceCandidate;
    }
    result.updateWorldTransforms(); return result;
}
double percentile(std::vector<double> values, double fraction) {
    require(!values.empty(), "No timing samples");
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1];
}
void compare(const SimulationCacheFrame& left, const SimulationCacheFrame& right) {
    require(left.entities.size() == right.entities.size(), "Pose entity count mismatch");
    for (std::size_t i = 0; i < left.entities.size(); ++i) {
        const auto& a = left.entities[i]; const auto& b = right.entities[i];
        require(a.id == b.id && glm::length(a.transform.translation - b.transform.translation) <= 1e-5f,
            "Pose translation mismatch");
        const glm::quat qa(glm::radians(a.transform.rotationDegrees)), qb(glm::radians(b.transform.rotationDegrees));
        require(std::abs(1.0f - std::abs(glm::dot(qa, qb))) <= 1e-5f, "Pose rotation mismatch");
    }
}
}
int main(int argc, char** argv) {
    try {
        const auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path() / "physics-acceptance";
        std::filesystem::create_directories(root);
        auto registry = createBuiltinModuleRegistry();
        require(registry.contains(rigidBodyModuleId), "Physics acceptance requires IRIS_ENABLE_PHYSX=ON");
        const char* names[]{"14_physics_free_fall", "15_physics_bounce", "16_physics_stack"};
        std::ofstream metrics(root / "metrics.csv");
        metrics << "fixture,frames,bodies,solver_p50_ms,solver_p95_ms,advance_p50_ms,advance_p95_ms,min_dynamic_y,freefall_error_m,bounce_apex_m,stack_final_error_m,stack_last_second_displacement_m,stack_estimated_speed_mps\n";
        for (int fixture = 0; fixture < 3; ++fixture) {
            std::string error;
            RenderJob job;
            require(loadRenderJob(std::filesystem::path(MYRENDERER_SOURCE_DIR) / "assets/renderjobs"
                / (std::string(names[fixture]) + ".renderjob"), job, error), error);
            SceneDocument document; require(loadSceneDocument(job.scenePath, document, error), error);
            const Scene edit = authoredScene(document, job.scenePath);
            ModuleRuntime runtime(registry);
            require(runtime.configure(job.module.id, job.module.parameters, job.module.seed, error), error);
            require(runtime.reset(edit, 0, job.endFrame, 30, error), error);
            std::vector<double> solver, advance;
            for (int frame = 1; frame <= job.endFrame; ++frame) {
                const auto before = std::chrono::steady_clock::now();
                require(runtime.runToFrame(frame, error), error);
                advance.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count());
                solver.push_back(runtime.report().simulation.lastAdvanceMilliseconds);
            }
            SimulationCache cache, repeated, batch, sixty;
            require(runtime.bakeSimulation(edit, 0, job.endFrame, 30, cache, error), error);
            require(runtime.bakeSimulation(edit, 0, job.endFrame, 30, repeated, error), error);
            require(bakeJobSimulation(job, &registry, batch, error), error);
            SceneDocument relocated = document;
            for (auto& entity : relocated.entities)
                if (!entity.modelResource.empty() && entity.modelResource.rfind("builtin:", 0) != 0)
                    entity.modelResource = resolveSceneResource(entity.modelResource, job.scenePath).generic_u8string();
            const auto relocatedPath = root / (std::string(names[fixture]) + "-relocated.myscene");
            require(saveSceneDocument(relocatedPath, relocated, error), error);
            auto relocatedJob = job; relocatedJob.scenePath = relocatedPath;
            SimulationCache relocatedCache;
            require(bakeJobSimulation(relocatedJob, &registry, relocatedCache, error), error);
            require(batch.key.sceneContentHash == relocatedCache.key.sceneContentHash,
                "Saving/reopening a scene changed its resolved runtime asset identity");
            require(runtime.bakeSimulation(edit, 0, job.endFrame * 2, 60, sixty, error), error);
            require(cache.frames.size() == static_cast<std::size_t>(job.endFrame + 1), "Missing output frames");
            for (std::size_t frame = 0; frame < cache.frames.size(); ++frame) {
                compare(cache.frames[frame], repeated.frames[frame]);
                compare(cache.frames[frame], batch.frames[frame]);
                compare(cache.frames[frame], sixty.frames[frame * 2]);
                require(cache.frames[frame].contentHash == repeated.frames[frame].contentHash
                    && cache.frames[frame].contentHash == batch.frames[frame].contentHash, "Repeated/Batch hash mismatch");
            }
            require(saveSimulationCache(root / (std::string(names[fixture]) + "-cache.json"), cache, error), error);
            require(runtime.reset(edit, 0, job.endFrame, 30, error), error);
            for (const auto& frame : cache.frames) {
                require(runtime.applyCachedFrame(frame, error), error);
                require(!runtime.report().simulation.measured && runtime.report().contentHash == frame.contentHash,
                    "Cache replay changed poses or claimed solver measurements");
            }
            require(runtime.applyCachedFrame(cache.frames[0], error), error);
            require(runtime.runToFrame(job.endFrame, error) && runtime.report().contentHash == cache.frames.back().contentHash,
                "Backward pose replay/solver recompute mismatch");
            float minimum = 1e6f, apex = 0, freefall = 0, finalError = 0, displacement = 0, speed = 0;
            bool rising = false;
            for (std::size_t frame = 0; frame < cache.frames.size(); ++frame) {
                for (std::size_t i = 0; i < document.entities.size(); ++i) {
                    if (document.entities[i].rigidBody->type != RigidBodyType::Dynamic) continue;
                    const auto position = cache.frames[frame].entities[i].transform.translation;
                    minimum = std::min(minimum, position.y);
                    if (fixture == 1 && frame > 0) {
                        const auto previous = cache.frames[frame - 1].entities[i].transform.translation;
                        if (!rising && position.y > previous.y + 1e-4f) rising = true;
                        if (rising) {
                            apex = std::max(apex, position.y);
                            if (position.y < previous.y - 1e-4f) break;
                        }
                    }
                    if (fixture == 2 && frame >= 90) {
                        displacement = std::max(displacement, glm::length(position - cache.frames[90].entities[i].transform.translation));
                        if (frame > 90) speed = std::max(speed, 30.0f * glm::length(position - cache.frames[frame - 1].entities[i].transform.translation));
                        if (frame == 120) finalError = std::max(finalError, std::abs(position.y - (0.5f + static_cast<float>(i - 1))));
                    }
                }
            }
            if (fixture == 0) {
                freefall = std::abs(cache.frames.back().entities[0].transform.translation.y - 5.095f);
                require(freefall <= 0.05f, "Freefall analytic error exceeded 0.05 m");
            }
            if (fixture == 1) require(rising && minimum >= 0.47f && apex >= 0.90f && apex <= 1.35f,
                "Bounce trajectory exceeded frozen penetration/apex limits");
            if (fixture == 2) require(minimum >= 0.47f && finalError <= 0.08f && displacement <= 0.02f && speed <= 0.05f,
                "Stack trajectory exceeded frozen penetration/settling limits");
            const double solver95 = percentile(solver, .95);
            metrics << names[fixture] << ',' << cache.frames.size() << ',' << document.entities.size() << ','
                << percentile(solver, .5) << ',' << solver95 << ',' << percentile(advance, .5) << ',' << percentile(advance, .95)
                << ',' << minimum << ',' << freefall << ',' << apex << ',' << finalError << ',' << displacement << ',' << speed << '\n';
            metrics.flush();
            require(solver95 <= 2.0, "Solver P95 exceeded frozen 2 ms budget");
            std::cout << names[fixture] << ": PASS, frames=" << cache.frames.size() << ", solver P95=" << solver95 << " ms\n";
        }
        require(static_cast<bool>(metrics), "Cannot write physics metrics");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
