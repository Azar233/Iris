#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct CameraOrbitState {
    glm::vec3 target{0.0f};
    float yawDegrees{42.9718f};
    float pitchDegrees{20.0535f};
    float distance{3.2f};
    float fieldOfViewDegrees{45.0f};
    float farPlane{100.0f};
};

class Camera {
public:
    Camera();

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspectRatio) const;
    glm::vec3 position() const;

    void orbit(float yawDelta, float pitchDelta);
    void pan(float xDelta, float yDelta);
    void zoom(float wheelDelta);
    // Moves the camera and its orbit target together in the current view basis. This preserves
    // yaw, pitch and orbit distance, so WASD navigation can coexist with the existing orbit/pan
    // controls and saved CameraOrbitState without introducing a second camera mode.
    void moveLocal(float forwardDelta, float rightDelta, float upDelta = 0.0f);
    void reset(const glm::vec3& target = glm::vec3(0.0f));
    void setOrbitPose(
        const glm::vec3& target,
        float yawDegrees,
        float pitchDegrees,
        float distance,
        float fieldOfViewDegrees
    );

    CameraOrbitState orbitState() const;
    void setOrbitState(const CameraOrbitState& state);

    float fieldOfView() const { return fieldOfViewDegrees_; }
    void setFieldOfView(float degrees);
    void setFarPlane(float distance);

    // The projection's own depth range. Every consumer that has to agree with what the camera
    // rasterises -- shadow cascade fitting, depth reconstruction, reverse-engineering a view ray --
    // reads these instead of keeping a second copy of the numbers, because a cascade fitted around
    // a different near plane than the one being rendered is invisible until a shadow goes missing.
    float nearPlane() const { return nearPlane_; }
    float farPlane() const { return farPlane_; }

    // The view basis in world space. `forward` points from the camera towards its target, matching
    // `viewMatrix()`'s -Z axis; `right` and `up` are the other two axes. Exposed because consumers
    // that build their own cameras or frusta -- shadow cascades, for one -- have to use the same
    // basis the projection was built from rather than re-deriving it.
    glm::vec3 forwardDirection() const;
    glm::vec3 rightDirection() const;
    glm::vec3 upDirection() const;

private:
    glm::vec3 target_{0.0f};
    float yawRadians_{0.75f};
    float pitchRadians_{0.35f};
    float distance_{3.2f};
    float fieldOfViewDegrees_{45.0f};
    float nearPlane_{0.1f};
    float farPlane_{100.0f};
};
