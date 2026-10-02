#include "render/Camera.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(const glm::vec3& left, const glm::vec3& right) {
    return glm::length(left - right) < 1.0e-5f;
}

} // namespace

int main() {
    try {
        Camera camera;
        camera.setOrbitPose(glm::vec3(2.0f, 1.0f, -3.0f), 30.0f, -12.0f, 8.0f, 52.0f);
        camera.setFarPlane(1600.0f);
        const CameraOrbitState before = camera.orbitState();
        const glm::vec3 beforePosition = camera.position();
        const glm::vec3 forward = camera.forwardDirection();
        const glm::vec3 right = camera.rightDirection();

        camera.moveLocal(3.5f, -1.25f);
        const glm::vec3 expectedDelta = forward * 3.5f - right * 1.25f;
        const CameraOrbitState after = camera.orbitState();
        require(close(after.target, before.target + expectedDelta),
            "local movement must translate the orbit target in the view basis");
        require(close(camera.position(), beforePosition + expectedDelta),
            "local movement must translate the camera with its target");
        require(std::abs(after.yawDegrees - before.yawDegrees) < 1.0e-5f
                && std::abs(after.pitchDegrees - before.pitchDegrees) < 1.0e-5f
                && std::abs(after.distance - before.distance) < 1.0e-5f
                && std::abs(after.fieldOfViewDegrees - before.fieldOfViewDegrees) < 1.0e-5f,
            "WASD movement must preserve orbit orientation, distance and field of view");
        require(std::abs(after.farPlane - 1600.0f) < 1.0e-5f,
            "local movement must preserve the saved far clip distance");

        camera.moveLocal(0.0f, 0.0f, 2.0f);
        require(close(camera.orbitState().target,
            before.target + expectedDelta + glm::vec3(0.0f, 2.0f, 0.0f)),
            "optional vertical movement must follow world up");
        std::cout << "Camera local navigation: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Camera local navigation failed: " << error.what() << '\n';
        return 1;
    }
}
