#include "render/Camera.h"

#include <algorithm>
#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

Camera::Camera() = default;

glm::vec3 Camera::position() const {
    const float horizontal = std::cos(pitchRadians_);
    return target_ + distance_ * glm::vec3(
        horizontal * std::sin(yawRadians_),
        std::sin(pitchRadians_),
        horizontal * std::cos(yawRadians_)
    );
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position(), target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::vec3 Camera::forwardDirection() const {
    return glm::normalize(target_ - position());
}

glm::vec3 Camera::rightDirection() const {
    const glm::vec3 forward = forwardDirection();
    // Guard the degenerate case where the camera looks straight down: `cross(forward, worldUp)` would
    // collapse and the basis would be unusable.
    if (std::abs(glm::dot(forward, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.9999f) {
        return glm::vec3(1.0f, 0.0f, 0.0f);
    }
    return glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 Camera::upDirection() const {
    return glm::normalize(glm::cross(rightDirection(), forwardDirection()));
}

glm::mat4 Camera::projectionMatrix(float aspectRatio) const {
    return glm::perspective(
        glm::radians(fieldOfViewDegrees_), std::max(aspectRatio, 0.01f), nearPlane_, farPlane_
    );
}

void Camera::orbit(float yawDelta, float pitchDelta) {
    yawRadians_ += yawDelta;
    pitchRadians_ = std::clamp(pitchRadians_ + pitchDelta, -1.5f, 1.5f);
}

void Camera::pan(float xDelta, float yDelta) {
    const glm::vec3 forward = glm::normalize(target_ - position());
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const float speed = distance_ * 0.0015f;
    target_ += (-xDelta * right + yDelta * up) * speed;
}

void Camera::zoom(float wheelDelta) {
    distance_ *= std::pow(0.86f, wheelDelta);
    distance_ = std::clamp(distance_, 0.35f, 40.0f);
}

void Camera::moveLocal(float forwardDelta, float rightDelta, float upDelta) {
    target_ += forwardDirection() * forwardDelta
        + rightDirection() * rightDelta
        + glm::vec3(0.0f, 1.0f, 0.0f) * upDelta;
}

void Camera::reset(const glm::vec3& target) {
    target_ = target;
    yawRadians_ = 0.75f;
    pitchRadians_ = 0.35f;
    distance_ = 3.2f;
    fieldOfViewDegrees_ = 45.0f;
}

void Camera::setOrbitPose(
    const glm::vec3& target,
    float yawDegrees,
    float pitchDegrees,
    float distance,
    float fieldOfViewDegrees
) {
    target_ = target;
    yawRadians_ = glm::radians(yawDegrees);
    pitchRadians_ = std::clamp(glm::radians(pitchDegrees), -1.5f, 1.5f);
    distance_ = std::clamp(distance, 0.35f, 40.0f);
    setFieldOfView(fieldOfViewDegrees);
}

CameraOrbitState Camera::orbitState() const {
    return CameraOrbitState{
        target_,
        glm::degrees(yawRadians_),
        glm::degrees(pitchRadians_),
        distance_,
        fieldOfViewDegrees_
    };
}

void Camera::setOrbitState(const CameraOrbitState& state) {
    setOrbitPose(
        state.target,
        state.yawDegrees,
        state.pitchDegrees,
        state.distance,
        state.fieldOfViewDegrees
    );
}

void Camera::setFieldOfView(float degrees) {
    fieldOfViewDegrees_ = std::clamp(degrees, 15.0f, 90.0f);
}
