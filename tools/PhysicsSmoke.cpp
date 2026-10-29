// P1-A CPU SDK development check. No renderer, window, CUDA or Scene dependency.
#include <PxPhysicsAPI.h>

#include <cmath>
#include <iostream>
#include <memory>

namespace {
struct Release {
    template<class T> void operator()(T* value) const { if (value) value->release(); }
};
template<class T> using Owner = std::unique_ptr<T, Release>;

bool run() {
    using namespace physx;
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errors;
    Owner<PxFoundation> foundation(PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errors));
    if (!foundation) return false;
    Owner<PxPhysics> physics(PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), false, nullptr));
    if (!physics) return false;
    Owner<PxDefaultCpuDispatcher> dispatcher(PxDefaultCpuDispatcherCreate(1));
    if (!dispatcher) return false;
    PxSceneDesc description(physics->getTolerancesScale());
    description.gravity = PxVec3(0.0f, -9.81f, 0.0f);
    description.cpuDispatcher = dispatcher.get();
    description.filterShader = PxDefaultSimulationFilterShader;
    Owner<PxScene> scene(physics->createScene(description));
    if (!scene) return false;
    Owner<PxMaterial> material(physics->createMaterial(0.5f, 0.5f, 0.5f));
    if (!material) return false;
    Owner<PxRigidDynamic> sphere(physics->createRigidDynamic(PxTransform(PxVec3(0.0f, 10.0f, 0.0f))));
    if (!sphere) return false;
    Owner<PxShape> shape(physics->createShape(PxSphereGeometry(0.5f), *material, true));
    if (!shape || !sphere->attachShape(*shape)) return false;
    if (!PxRigidBodyExt::updateMassAndInertia(*sphere, 1.0f)) return false;
    sphere->setLinearDamping(0.0f);
    scene->addActor(*sphere);
    constexpr float step = 1.0f / 120.0f;
    // Frame zero is the authored initial pose. Only simulate advances time.
    if (sphere->getGlobalPose().p.y != 10.0f) return false;
    for (int i = 0; i < 120; ++i) {
        scene->simulate(step);
        if (!scene->fetchResults(true)) return false;
    }
    const float height = sphere->getGlobalPose().p.y;
    const float velocity = sphere->getLinearVelocity().y;
    std::cout << "PhysX " << PX_PHYSICS_VERSION_MAJOR << '.' << PX_PHYSICS_VERSION_MINOR
              << '.' << PX_PHYSICS_VERSION_BUGFIX << " CPU; steps=120; dt=1/120 s; y="
              << height << " m; vy=" << velocity << " m/s\n";
    return std::isfinite(height) && std::isfinite(velocity)
        && std::abs(height - 5.095f) < 0.05f && std::abs(velocity + 9.81f) < 0.001f;
}
} // namespace

int main() {
    // Exercise complete teardown before creating a second foundation/scene.
    if (!run() || !run()) {
        std::cerr << "PhysX CPU initialization/stepping/teardown check failed\n";
        return 1;
    }
    return 0;
}
