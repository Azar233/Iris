#include "module/BuiltinModules.h"
#include "runtime/BatchRuntime.h"
#include "scene/SceneDocument.h"
#include "app/RigidBodyEditing.h"
#include "scene/RigidBodyDebug.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
Scene fallingScene(bool ground = false) {
    Scene scene;
    auto* sphere = scene.find(scene.createEntityWithId(1, "Sphere"));
    sphere->transform.translation.y = 10.0f;
    sphere->rigidBody = RigidBodyComponent{};
    sphere->rigidBody->shape = CollisionShape::Sphere;
    sphere->rigidBody->restitution = 0.5f;
    if (ground) {
        auto* floor = scene.find(scene.createEntityWithId(2, "Ground"));
        floor->rigidBody = RigidBodyComponent{};
        floor->rigidBody->type = RigidBodyType::Static;
        floor->rigidBody->shape = CollisionShape::Plane;
        floor->rigidBody->restitution = 0.5f;
    }
    scene.updateWorldTransforms();
    return scene;
}
SceneDocument documentFor(const Scene& scene) {
    SceneDocument doc;
    doc.moduleId = rigidBodyModuleId;
    for (const auto& source : scene.entities()) {
        SceneDocumentEntity entity;
        entity.id = source.id; entity.name = source.name;
        entity.transform = source.transform; entity.rigidBody = source.rigidBody;
        doc.entities.push_back(entity);
    }
    return doc;
}
float height(const ModuleRuntime& runtime) { return runtime.runtimeScene().scene().find(1)->transform.translation.y; }

void run() {
    const auto directory = std::filesystem::temp_directory_path() / "IrisPhysicsDevelopment";
    std::filesystem::create_directories(directory);
    const auto path = directory / "bodies.myscene";
    std::string error;
    const Scene edit = fallingScene();
    Scene edited = fallingScene();
    const auto originalHash = sceneContentHash(edited);
    auto invalidBody = *edited.find(1)->rigidBody; invalidBody.mass = -1;
    require(!setEntityRigidBody(edited, 1, invalidBody, rigidBodyModuleId, error)
        && sceneContentHash(edited) == originalHash, "Rejected editor body edit changed the scene");
    require(setEntityRigidBody(edited, 1, std::nullopt, rigidBodyModuleId, error)
        && !edited.find(1)->rigidBody, "Editor could not remove body");
    require(!setEntityRigidBody(edited, 1, RigidBodyComponent{}, "", error), "Editor attached body without physics module");
    require(setEntityRigidBody(edited, 1, RigidBodyComponent{}, rigidBodyModuleId, error), error);
    edited.find(1)->parent = 42;
    require(!setEntityRigidBody(edited, 1, RigidBodyComponent{}, rigidBodyModuleId, error), "Editor accepted parented body");
    edited.find(1)->parent = 0;
    for (int id = 2; id <= 16; ++id) {
        edited.createEntityWithId(static_cast<SceneEntityId>(id), "Body");
        require(setEntityRigidBody(edited, static_cast<SceneEntityId>(id), RigidBodyComponent{}, rigidBodyModuleId, error), error);
    }
    edited.createEntityWithId(17, "Excess");
    require(!setEntityRigidBody(edited, 17, RigidBodyComponent{}, rigidBodyModuleId, error)
        && !edited.find(17)->rigidBody, "Editor exceeded dynamic body limit");
    auto debugEntity = *edit.find(1);
    debugEntity.transform.assetTransform = glm::mat4(100.0f);
    const auto sphereLines = rigidBodyDebugLines(debugEntity);
    require(sphereLines.size() == 96, "Sphere overlay lost its three rings");
    for (const auto& line : sphereLines)
        require(std::abs(glm::length(line.from - debugEntity.transform.translation) - 0.5f) < 1.0e-5f,
            "Display assetMatrix changed collision overlay radius");
    debugEntity.rigidBody->shape = CollisionShape::Box;
    require(rigidBodyDebugLines(debugEntity).size() == 12, "Box overlay lost edges");
    debugEntity.rigidBody->shape = CollisionShape::Plane;
    require(rigidBodyDebugLines(debugEntity).size() == 23, "Plane overlay lost grid or normal");
    SceneDocument doc = documentFor(edit), loaded;
    auto& component = *doc.entities[0].rigidBody;
    component.offset = glm::vec3(0.1f, 0.2f, 0.3f);
    component.linearVelocity = glm::vec3(1.0f, 2.0f, 3.0f);
    component.angularVelocity = glm::vec3(0.3f, 0.2f, 0.1f);
    require(saveSceneDocument(path, doc, error), error);
    require(loadSceneDocument(path, loaded, error), error);
    require(loaded.entities[0].rigidBody->offset == component.offset
        && loaded.entities[0].rigidBody->angularVelocity == component.angularVelocity
        && loaded.entities[0].rigidBody->linearVelocity == component.linearVelocity, "Body roundtrip lost vectors");
    const auto oldMass = component.mass;
    component.mass = -1.0f;
    require(!saveSceneDocument(path, doc, error), "Negative mass was accepted");
    require(loadSceneDocument(path, loaded, error) && loaded.entities[0].rigidBody->mass == oldMass,
        "Rejected save replaced the last valid scene");
    component.mass = oldMass;
    doc.entities[0].transform.scale.x = 2.0f;
    require(!saveSceneDocument(path, doc, error), "Non-unit physics scale was accepted");
    doc.entities[0].transform.scale = glm::vec3(1.0f);
    component.radius = std::numeric_limits<float>::infinity();
    require(!saveSceneDocument(path, doc, error), "Infinite radius was accepted");
    std::ofstream(path) << R"({"format":"MyRendererScene","version":2,"module":{"id":"iris.physics.rigid-body"},"entities":[{"id":1,"rigidBody":{"version":1,"radius":-1}}]})";
    require(!loadSceneDocument(path, loaded, error) && loaded.entities[0].rigidBody->mass == oldMass,
        "Rejected load replaced the existing document");
    std::ofstream(path) << R"({"format":"MyRendererScene","version":1,"entities":[{"id":1}]})";
    require(loadSceneDocument(path, loaded, error) && !loaded.entities[0].rigidBody, "Legacy scene enabled physics");

    Scene changed = fallingScene();
    changed.find(1)->rigidBody->mass = 2.0f;
    require(sceneContentHash(changed) != sceneContentHash(edit), "Body changes did not invalidate cache identity");
    RuntimeScene copy;
    copy.resetFrom(edit);
    require(copy.contentHash() == sceneContentHash(edit) && copy.scene().find(1)->rigidBody.has_value(),
        "Runtime copy lost body data");

    auto registry = createBuiltinModuleRegistry();
    const char* fixtureJobs[]{"14_physics_free_fall", "15_physics_bounce", "16_physics_stack"};
    const std::size_t fixtureBodyCounts[]{1, 2, 4};
    for (std::size_t index = 0; index < 3; ++index) {
        RenderJob fixtureJob;
        const auto fixturePath = std::filesystem::path(MYRENDERER_SOURCE_DIR) / "assets/renderjobs"
            / (std::string(fixtureJobs[index]) + ".renderjob");
        require(loadRenderJob(fixturePath, fixtureJob, error), error);
        SceneDocument fixture;
        require(loadSceneDocument(fixtureJob.scenePath, fixture, error), error);
        require(fixture.moduleId == rigidBodyModuleId && fixtureJob.module.id == fixture.moduleId
            && fixture.moduleSeed == 20261026U && fixtureJob.module.seed == fixture.moduleSeed
            && fixtureJob.startFrame == 0 && fixtureJob.framesPerSecond == 30
            && fixtureJob.endFrame == (index == 0 ? 30 : 120)
            && fixture.entities.size() == fixtureBodyCounts[index]
            && fixture.moduleParameters.size() == 3 && fixtureJob.module.parameters.size() == 3,
            "Physics fixture identity/range/body count drifted");
        for (const auto& entity : fixture.entities) {
            require(entity.rigidBody.has_value(), "Fixture entity lost collision configuration");
            const auto& body = *entity.rigidBody;
            require(body.linearDamping == 0 && body.linearVelocity == glm::vec3(0)
                && body.angularVelocity == glm::vec3(0) && entity.parent == 0
                && entity.transform.scale == glm::vec3(1), "Fixture initial conditions drifted");
            const bool ground = body.type == RigidBodyType::Static;
            const float initialHeight = ground ? 0.0f : index == 0 ? 10.0f : index == 1 ? 3.0f
                : 0.5f + static_cast<float>(entity.id - 2) * 1.01f;
            require(std::abs(entity.transform.translation.y - initialHeight) < 1.0e-6f
                && entity.transform.translation.x == 0 && entity.transform.translation.z == 0,
                "Fixture initial position drifted");
            require(body.shape == (ground ? CollisionShape::Plane
                : index == 2 ? CollisionShape::Box : CollisionShape::Sphere)
                && body.restitution == (index == 1 ? 0.5f : 0.0f),
                "Fixture shape/material drifted");
        }
        const bool valid = validateRenderJobAssets(fixtureJob, error, &registry);
        if (registry.contains(rigidBodyModuleId)) require(valid, error);
        else require(!valid && error.find("IRIS_ENABLE_PHYSX=ON") != std::string::npos,
            "Fixture OFF validation lacked physics diagnostic");
    }
    ModuleRuntime runtime(registry);
    if (!registry.contains(rigidBodyModuleId)) {
        require(!runtime.configure(rigidBodyModuleId, {}, 1, error)
            && error.find("IRIS_ENABLE_PHYSX=ON") != std::string::npos, "OFF lacked actionable physics diagnostic");
        require(saveSceneDocument(path, documentFor(edit), error), error);
        RenderJob job; job.scenePath = path; job.module.id = rigidBodyModuleId;
        SimulationCache cache;
        require(!bakeJobSimulation(job, &registry, cache, error)
            && error.find("IRIS_ENABLE_PHYSX=ON") != std::string::npos, "OFF Batch silently accepted physics");
        std::cout << "Physics schema/legacy/transactions/cache identity/OFF diagnostics passed\n";
        return;
    }
    require(runtime.configure(rigidBodyModuleId, {}, 1, error), error);
    require(runtime.reset(edit, 0, 120, 30, error), error);
    require(height(runtime) == 10.0f, "Frame zero advanced physics");
    const auto& config = runtime.report().simulationConfiguration;
    require(config.sdkVersion == "PhysX 5.6.1" && config.solver == "PGS"
        && config.cacheSemantics == "poses-only" && config.substepsPerFrame == 4
        && config.fixedStepSeconds == 1.0 / 120.0 && config.positionIterations == 8
        && config.velocityIterations == 2 && config.bodyCount == 1 && config.dynamicBodyCount == 1,
        "Report lost effective physics configuration");
    require(runtime.report().simulation.totalSubsteps == 0 && runtime.report().simulation.bodyCount == 1,
        "Initial solver statistics are incorrect");
    require(runtime.runToFrame(30, error), error);
    const float atOneSecond = height(runtime);
    require(runtime.report().simulation.measured && runtime.report().simulation.totalSubsteps == 120 && runtime.report().simulation.lastAdvanceSubsteps == 4
        && std::isfinite(runtime.report().simulation.lastAdvanceMilliseconds), "Solver statistics lost fixed substeps");
    runtime.runtimeScene().scene().beginFrame();
    require(runtime.runToFrame(31, error), error);
    require(runtime.runtimeScene().scene().find(1)->motionHistoryValid
        && std::abs(runtime.runtimeScene().scene().find(1)->previousWorldTransform[3].y - atOneSecond) < 1.0e-5f,
        "Continuous display frame did not preserve previous pose");
    require(std::abs(atOneSecond - 5.095f) < 0.05f && edit.find(1)->transform.translation.y == 10.0f,
        "Freefall mismatch or edit scene mutated");
    require(runtime.reset(edit, 0, 120, 60, error) && runtime.runToFrame(60, error), error);
    require(std::abs(height(runtime) - atOneSecond) < 1.0e-5f, "Output FPS changed same-time physics pose");
    require(!runtime.reset(edit, 0, 120, 29, error), "Non-integer substeps were accepted");
    require(runtime.reset(edit, 0, 120, 30, error) && runtime.runToFrame(30, error), error);
    require(std::abs(height(runtime) - atOneSecond) < 1.0e-5f, "Reset replay changed trajectory");

    SimulationCache cache;
    require(runtime.bakeSimulation(edit, 0, 31, 30, cache, error), error);
    require(cache.frames.size() == 32 && cache.frames[0].entities[0].transform.translation.y == 10.0f,
        "Pose bake lost initial frame");
    require(runtime.reset(edit, 0, 120, 30, error) && runtime.applyCachedFrame(cache.frames[30], error), error);
    require(runtime.runToFrame(31, error), error);
    require(runtime.report().contentHash == cache.frames[31].contentHash, "Cache continuation reused poses as solver state");
    require(runtime.applyCachedFrame(cache.frames[2], error) && runtime.runToFrame(3, error), error);
    require(runtime.report().contentHash == cache.frames[3].contentHash, "Earlier cached pose did not recompute solver");
    const auto beforeRejectedPose = runtime.report().contentHash;
    auto invalidPose = cache.frames[3];
    invalidPose.entities[0].transform.scale.x = 2.0f;
    Scene scaled = runtime.runtimeScene().scene();
    scaled.find(1)->transform = invalidPose.entities[0].transform;
    invalidPose.contentHash = sceneContentHash(scaled);
    require(!runtime.applyCachedFrame(invalidPose, error)
        && runtime.runtimeScene().contentHash() == beforeRejectedPose && runtime.timeline().frame() == 3,
        "Self-consistent cached hash bypassed rigid body scale contract");
    require(runtime.applyCachedFrame(cache.frames[3], error) && !runtime.report().simulation.measured
        && runtime.report().simulationConfiguration.cacheSemantics == "poses-only"
        && runtime.report().simulation.totalSubsteps == 0,
        "Valid cache replay did not recover or clear solver diagnostics");

    Timeline loop;
    loop.setFrameRange(10, 13); loop.setFramesPerSecond(30); loop.setLoop(true);
    require(runtime.reset(edit, 10, 13, 30, error) && runtime.runToFrame(13, error), error);
    const auto endHash = runtime.report().contentHash;
    require(runtime.runToFrame(13, error) && runtime.report().contentHash == endHash
        && runtime.report().simulation.totalSubsteps == 12, "Holding last frame stepped physics again");
    require(!runtime.runToFrame(14, error) && runtime.report().contentHash == endHash
        && runtime.timeline().frame() == 13, "Past-end request changed last pose");
    for (int cycle = 0; cycle < 3; ++cycle) {
        loop.setFrame(13); require(loop.advance() && loop.frame() == 10, "Loop did not reach initial frame");
        require(runtime.reset(edit, 10, 13, 30, error) && runtime.runToFrame(loop.frame(), error), error);
        require(height(runtime) == 10.0f && runtime.report().simulation.totalSubsteps == 0, "Loop advanced initial pose");
        require(runtime.runToFrame(13, error) && runtime.report().contentHash == endHash, "Loop recompute changed trajectory");
    }
    SimulationCache terminal;
    const int maximum = std::numeric_limits<int>::max();
    require(runtime.bakeSimulation(edit, maximum, maximum, 30, terminal, error)
        && terminal.frames.size() == 1 && terminal.frames[0].frame == maximum
        && terminal.frames[0].entities[0].transform.translation.y == 10.0f,
        "Single terminal-frame bake overflowed or advanced physics");

    require(saveSceneDocument(path, documentFor(edit), error), error);
    RenderJob job; job.scenePath = path; job.module.id = rigidBodyModuleId; job.module.seed = 1;
    job.startFrame = 0; job.endFrame = 31; job.framesPerSecond = 30;
    SimulationCache batch;
    require(bakeJobSimulation(job, &registry, batch, error), error);
    require(batch.frames.back().contentHash == cache.frames.back().contentHash, "Batch diverged from shared Module runtime");
    job.renderSettings.width = 16; job.renderSettings.height = 16;
    job.renderSettings.samplesPerPixel = 1;
    job.outputFormats = {pathtracer::RenderFileFormat::Png};
    const auto prepareReportOutput = [&](const char* name) {
        job.outputStemPattern = path.parent_path() / name;
        // Only this test's exact managed outputs; keep the product's overwrite guard.
        std::filesystem::remove(renderJobFramePath(job, 1, ".png"));
        std::filesystem::remove(renderJobFramePath(job, 1, "-report.json"));
    };
    prepareReportOutput("physics-report-solved");
    BatchFrameResult rendered;
    require(runRenderJobFrame(job, 1, rendered, error, nullptr, &registry), error);
    const auto readReport = [&] {
        std::ifstream file(renderJobFramePath(job, 1, "-report.json"));
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    require(rendered.module.simulation.measured && rendered.module.simulation.totalSubsteps == 4
        && readReport().find("\"substepsPerFrame\": 4") != std::string::npos
        && readReport().find("\"measured\": true") != std::string::npos,
        "CPU sidecar lost solved physics configuration/statistics");
    job.simulationCache = path.parent_path() / "physics-report-cache.json";
    require(saveSimulationCache(job.simulationCache, batch, error), error);
    prepareReportOutput("physics-report-cached");
    require(runRenderJobFrame(job, 1, rendered, error, nullptr, &registry), error);
    require(rendered.cacheStatus == SimulationCacheStatus::Hit && !rendered.module.simulation.measured
        && readReport().find("\"measured\": false") != std::string::npos
        && readReport().find("lastAdvanceMilliseconds") == std::string::npos,
        "CPU cache sidecar claimed solver measurements");
    job.simulationCache.clear();
    ModuleParameterValue gravity; gravity.number = 0.0f;
    job.module.parameters.push_back(ModuleParameterOverride{"gravityY", gravity});
    require(bakeJobSimulation(job, &registry, batch, error), error);
    require(batch.frames.back().entities[0].transform.translation.y == 10.0f
        && batch.key.parameterHash != cache.key.parameterHash, "Job gravity override was ignored or reused old cache identity");
    CancellationToken cancelled; cancelled.requestCancellation();
    require(!bakeJobSimulation(job, &registry, batch, error, &cancelled)
        && error.find("cancelled") != std::string::npos, "Batch bake did not propagate host cancellation");

    int polls = 0;
    require(runtime.reset(edit, 0, 120, 30, error), error);
    runtime.setCancellationCheck([&polls] { return ++polls >= 3; });
    require(!runtime.runToFrame(1, error) && runtime.report().status == ModuleRunStatus::Cancelled,
        "Substep cancellation was not reported");
    require(height(runtime) == 10.0f, "Cancelled substeps published a partial pose");
    runtime.setCancellationCheck({});
    require(runtime.runToFrame(1, error) && runtime.report().error.empty(), "Cancelled frame could not be retried");
    const Scene collisions = fallingScene(true);
    require(runtime.reset(collisions, 0, 120, 30, error) && runtime.runToFrame(120, error), error);
    require(height(runtime) >= 0.48f, "Sphere passed through static ground");
    std::cout << "Physics schema/initial pose/freefall/FPS/reset/pose cache/Batch/override/cancel/collision passed\n";
}
} // namespace
int main() {
    try { run(); return 0; }
    catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return 1; }
}
