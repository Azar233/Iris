#include "module/BuiltinModules.h"

#include <PxPhysicsAPI.h>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <chrono>
#include <stdexcept>

namespace {
using namespace physx;
struct Release {
    template<class T> void operator()(T* value) const { if (value) value->release(); }
};
template<class T> using Owner = std::unique_ptr<T, Release>;

// PhysX has one foundation per process, while each module owns a separate scene.
struct Sdk {
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errors;
    Owner<PxFoundation> foundation{PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errors)};
    Owner<PxPhysics> physics{foundation
        ? PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), false, nullptr) : nullptr};
};
std::shared_ptr<Sdk> acquireSdk() {
    // Function-local initialization is thread-safe; keep the foundation alive until
    // process exit so concurrent module destruction/creation cannot race it.
    static const auto sdk = std::make_shared<Sdk>();
    if (!sdk->physics) throw std::runtime_error("PhysX CPU SDK initialization failed");
    return sdk;
}
PxVec3 vector(const glm::vec3& v) { return PxVec3(v.x, v.y, v.z); }
PxTransform pose(const SceneTransform& transform) {
    const glm::quat rotation(glm::radians(transform.rotationDegrees));
    return PxTransform(vector(transform.translation), PxQuat(rotation.x, rotation.y, rotation.z, rotation.w));
}

class RigidBodyModule final : public ISceneModule {
public:
    ModuleManifest manifest() const override {
        ModuleManifest value;
        value.id = rigidBodyModuleId;
        value.displayName = "CPU Rigid Bodies (PhysX 5.6.1)";
        value.kind = ModuleKind::Simulation;
        value.cmakeTarget = "MyRendererModules";
        value.sourceRoot = "src/module/RigidBodyModule.cpp";
        value.buildId = moduleBuildId();
        return value;
    }
    void registerParameters(ParameterRegistry& parameters) override {
        parameters.registerFloat("gravityY", "Gravity Y (m/s2)", -9.81f, -100.0f, 0.0f,
            "CPU gravity; the simulation step is fixed at 1/120 second.");
        parameters.registerInt("positionIterations", "Position iterations", 8, 4, 32,
            "PhysX PGS position solver iterations.");
        parameters.registerInt("velocityIterations", "Velocity iterations", 2, 1, 16,
            "PhysX PGS velocity solver iterations.");
    }
    void reset() override {
        actors_.clear(); materials_.clear(); scene_.reset(); dispatcher_.reset(); sdk_.reset();
        inputs_.clear(); steps_ = 0; initialized_ = false;
        statistics_ = {};
    }
    bool initialize(const SceneContext& context, std::string& error) override {
        reset();
        try {
            fps_ = context.timeline().framesPerSecond();
            if (fps_ <= 0 || fps_ > 120 || 120 % fps_ != 0)
                throw std::runtime_error("Physics output FPS must divide 120 (for example 24, 30, 60 or 120)");
            start_ = context.timeline().startFrame();
            gravity_ = context.parameters().floatValue("gravityY", -9.81f);
            positionIterations_ = context.parameters().intValue("positionIterations", 8);
            velocityIterations_ = context.parameters().intValue("velocityIterations", 2);
            std::size_t dynamicCount = 0;
            for (const auto& entity : context.scene().entities()) if (entity.rigidBody) {
                validateRigidBody(*entity.rigidBody, entity.transform.translation, entity.transform.rotationDegrees,
                    entity.transform.scale, entity.parent != invalidSceneEntityId, entity.light.has_value());
                if (entity.rigidBody->type == RigidBodyType::Dynamic) ++dynamicCount;
                inputs_.push_back(Input{entity.id, entity.transform, *entity.rigidBody});
            }
            if (inputs_.empty() || inputs_.size() > 128U || dynamicCount > 16U)
                throw std::runtime_error("Physics requires 1..128 bodies, with at most 16 dynamic bodies");
            createWorld(context);
            initialized_ = true;
            error.clear(); return true;
        } catch (const std::exception& failure) {
            error = failure.what(); reset(); return false;
        }
    }
    bool fixedUpdate(SceneContext& context, double fixedDeltaSeconds, std::string& error) override {
        if (!initialized_) { error = "Physics module is not initialized"; return false; }
        try {
            if (std::abs(fixedDeltaSeconds - 1.0 / fps_) > 1.0e-9)
                throw std::runtime_error("Physics Timeline FPS changed; reset the module");
            const std::int64_t target = static_cast<std::int64_t>(context.timeline().frame() - start_) * (120 / fps_);
            if (target < 0) throw std::runtime_error("Physics frame precedes its initial frame");
            // Cache frames are presentation poses, never a solver checkpoint.
            if (target < steps_) createWorld(context);
            const auto started = std::chrono::steady_clock::now();
            const auto stepsBefore = steps_;
            while (steps_ < target) {
                if (context.cancellationRequested()) throw std::runtime_error("Physics simulation cancelled");
                scene_->simulate(1.0f / 120.0f);
                if (!scene_->fetchResults(true)) throw std::runtime_error("PhysX fetchResults failed");
                ++steps_;
            }
            if (context.cancellationRequested()) throw std::runtime_error("Physics simulation cancelled");
            statistics_.totalSubsteps = static_cast<std::uint64_t>(steps_);
            statistics_.measured = true;
            statistics_.lastAdvanceSubsteps = static_cast<std::uint64_t>(steps_ - stepsBefore);
            statistics_.bodyCount = inputs_.size();
            statistics_.lastAdvanceMilliseconds = steps_ == stepsBefore ? 0.0
                : std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            // Frame zero is untouched, including authored Euler representation.
            if (target == 0) { error.clear(); return true; }
            // Resolve every id before publishing any pose.
            for (const auto& input : inputs_) if (!context.scene().find(input.id))
                throw std::runtime_error("Physics entity disappeared; reset the module");
            std::vector<SceneTransform> poses;
            poses.reserve(inputs_.size());
            for (std::size_t i = 0; i < inputs_.size(); ++i) {
                poses.push_back(inputs_[i].transform);
                if (inputs_[i].body.type != RigidBodyType::Dynamic) continue;
                const PxTransform result = actors_[i]->getGlobalPose();
                if (!result.isValid()) throw std::runtime_error("PhysX produced an invalid pose");
                auto& transform = poses.back();
                transform.translation = glm::vec3(result.p.x, result.p.y, result.p.z);
                transform.rotationDegrees = glm::degrees(glm::eulerAngles(glm::quat(result.q.w, result.q.x, result.q.y, result.q.z)));
            }
            for (std::size_t i = 0; i < inputs_.size(); ++i)
                context.scene().find(inputs_[i].id)->transform = poses[i];
            context.runtimeScene().markDirty();
            error.clear(); return true;
        } catch (const std::exception& failure) { error = failure.what(); return false; }
    }
    std::string serializeState() const override {
        return "physx=5.6.1|solver=PGS|dt=1/120|steps=" + std::to_string(steps_)
            + "|bodies=" + std::to_string(inputs_.size()) + "|cache=poses-only";
    }
    ModuleSimulationStatistics simulationStatistics() const override { return statistics_; }
    ModuleSimulationConfiguration simulationConfiguration() const override {
        ModuleSimulationConfiguration result;
        result.sdkVersion = "PhysX 5.6.1";
        result.solver = "PGS";
        result.cacheSemantics = "poses-only";
        result.fixedStepSeconds = 1.0 / 120.0;
        result.gravityY = gravity_;
        result.substepsPerFrame = 120 / fps_;
        result.positionIterations = positionIterations_;
        result.velocityIterations = velocityIterations_;
        result.bodyCount = inputs_.size();
        for (const auto& input : inputs_)
            if (input.body.type == RigidBodyType::Dynamic) ++result.dynamicBodyCount;
        return result;
    }
private:
    struct Input { SceneEntityId id; SceneTransform transform; RigidBodyComponent body; };
    void createWorld(const SceneContext& context) {
        actors_.clear(); materials_.clear(); scene_.reset(); dispatcher_.reset();
        sdk_ = acquireSdk();
        dispatcher_.reset(PxDefaultCpuDispatcherCreate(1));
        if (!dispatcher_) throw std::runtime_error("PhysX CPU dispatcher creation failed");
        PxSceneDesc description(sdk_->physics->getTolerancesScale());
        description.gravity = PxVec3(0.0f, gravity_, 0.0f);
        description.cpuDispatcher = dispatcher_.get();
        description.filterShader = PxDefaultSimulationFilterShader;
        description.flags |= PxSceneFlag::eENABLE_ENHANCED_DETERMINISM;
        scene_.reset(sdk_->physics->createScene(description));
        if (!scene_) throw std::runtime_error("PhysX scene creation failed");
        for (const auto& input : inputs_) {
            if (context.cancellationRequested()) throw std::runtime_error("Physics initialization cancelled");
            const auto& body = input.body;
            Owner<PxMaterial> material(sdk_->physics->createMaterial(body.friction, body.friction, body.restitution));
            if (!material) throw std::runtime_error("PhysX material creation failed");
            PxGeometryHolder geometry;
            if (body.shape == CollisionShape::Sphere) geometry.storeAny(PxSphereGeometry(body.radius));
            else if (body.shape == CollisionShape::Box) geometry.storeAny(PxBoxGeometry(vector(body.halfExtents)));
            else geometry.storeAny(PxPlaneGeometry());
            Owner<PxShape> shape(sdk_->physics->createShape(geometry.any(), *material, true));
            if (!shape) throw std::runtime_error("PhysX shape creation failed");
            shape->setLocalPose(PxTransform(vector(body.offset), body.shape == CollisionShape::Plane
                ? PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f)) : PxQuat(PxIdentity)));
            Owner<PxRigidActor> actor(body.type == RigidBodyType::Static
                ? static_cast<PxRigidActor*>(sdk_->physics->createRigidStatic(pose(input.transform)))
                : static_cast<PxRigidActor*>(sdk_->physics->createRigidDynamic(pose(input.transform))));
            if (!actor || !actor->attachShape(*shape)) throw std::runtime_error("PhysX actor creation failed");
            if (auto* dynamic = actor->is<PxRigidDynamic>()) {
                if (!PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, body.mass))
                    throw std::runtime_error("PhysX mass/inertia setup failed");
                dynamic->setLinearDamping(body.linearDamping); dynamic->setAngularDamping(body.angularDamping);
                dynamic->setLinearVelocity(vector(body.linearVelocity)); dynamic->setAngularVelocity(vector(body.angularVelocity));
                dynamic->setSolverIterationCounts(static_cast<PxU32>(positionIterations_), static_cast<PxU32>(velocityIterations_));
            }
            scene_->addActor(*actor);
            materials_.push_back(std::move(material)); actors_.push_back(std::move(actor));
        }
        steps_ = 0;
    }
    std::vector<Input> inputs_;
    std::shared_ptr<Sdk> sdk_;
    Owner<PxDefaultCpuDispatcher> dispatcher_;
    Owner<PxScene> scene_;
    std::vector<Owner<PxMaterial>> materials_;
    std::vector<Owner<PxRigidActor>> actors_;
    std::int64_t steps_{0};
    int start_{0}, fps_{30}, positionIterations_{8}, velocityIterations_{2};
    float gravity_{-9.81f};
    bool initialized_{false};
    ModuleSimulationStatistics statistics_;
};
} // namespace

std::unique_ptr<ISceneModule> makeRigidBodyModule() { return std::make_unique<RigidBodyModule>(); }
