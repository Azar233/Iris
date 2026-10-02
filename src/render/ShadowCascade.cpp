#include "render/ShadowCascade.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace shadow {
namespace {

// Rounds a light-space offset to whole texels. Without this the shadow map's texel grid slides
// under the geometry as the camera moves, and every shadow edge shimmers; rounding in light space
// pins the grid to the light instead.
float snapToTexel(float value, float texelSize) {
    if (!(texelSize > 0.0f)) return value;
    return std::floor(value / texelSize) * texelSize;
}

} // namespace

std::array<float, maximumCascadeCount> splitDistances(
    float nearPlane,
    float farPlane,
    std::size_t count,
    float lambda
) {
    std::array<float, maximumCascadeCount> splits{};
    const std::size_t clamped = std::clamp<std::size_t>(count, 1U, maximumCascadeCount);
    const float clampedFar = std::max(farPlane, nearPlane);
    if (!(nearPlane > 0.0f) || !(clampedFar > nearPlane) || !std::isfinite(nearPlane)
        || !std::isfinite(clampedFar)) {
        splits.fill(clampedFar);
        return splits;
    }
    const float ratio = clampedFar / nearPlane;
    const float blend = std::clamp(lambda, 0.0f, 1.0f);
    for (std::size_t index = 1U; index <= clamped; ++index) {
        const float fraction = static_cast<float>(index) / static_cast<float>(clamped);
        const float logarithmic = nearPlane * std::pow(ratio, fraction);
        const float uniform = nearPlane + (clampedFar - nearPlane) * fraction;
        splits[index - 1U] = blend * logarithmic + (1.0f - blend) * uniform;
    }
    // A cascade cannot see further than the camera does, and the sequence has to stay ascending for
    // the per-fragment selection to be a simple ordered search.
    for (std::size_t index = 0U; index < clamped; ++index) {
        splits[index] = std::clamp(splits[index], nearPlane, clampedFar);
        if (index > 0U && splits[index] < splits[index - 1U]) splits[index] = splits[index - 1U];
    }
    for (std::size_t index = clamped; index < maximumCascadeCount; ++index) {
        splits[index] = clampedFar;
    }
    return splits;
}

std::array<glm::vec3, 8U> subFrustumCorners(
    const CameraFrustum& camera,
    float splitDistance
) {
    std::array<glm::vec3, 8U> corners{};

    // The sub-frustum is built from the camera's basis and its projection parameters rather than by
    // unprojecting NDC corners. Unprojection has to know which depth range the projection writes
    // (GLM's `perspective` writes a 0..1 range even for OpenGL), and a wrong answer there returns
    // points on the far side of the camera; a construction from the basis has no such convention to
    // get wrong and is exact at both ends of the slice.
    const float clampedNear = std::clamp(camera.nearPlane, 1.0e-4f, camera.farPlane);
    const float clampedFar = std::clamp(splitDistance, clampedNear, camera.farPlane);
    const float tangentHalfFov =
        std::tan(glm::radians(std::clamp(camera.fieldOfViewDegrees, 1.0f, 179.0f)) * 0.5f);
    const float aspect = std::max(camera.aspectRatio, 1.0e-3f);

    const auto planeCorners = [&](float distance, glm::vec3* out) {
        const float halfHeight = tangentHalfFov * distance;
        const float halfWidth = halfHeight * aspect;
        const glm::vec3 center = camera.origin + camera.forward * distance;
        out[0] = center - camera.right * halfWidth - camera.up * halfHeight;
        out[1] = center + camera.right * halfWidth - camera.up * halfHeight;
        out[2] = center + camera.right * halfWidth + camera.up * halfHeight;
        out[3] = center - camera.right * halfWidth + camera.up * halfHeight;
    };
    planeCorners(clampedNear, corners.data());
    planeCorners(clampedFar, corners.data() + 4U);
    return corners;
}

glm::mat4 buildLightView(
    const glm::vec3& pivot,
    const glm::vec3& directionToLight,
    float depthRange
) {
    const float directionLengthSquared = glm::dot(directionToLight, directionToLight);
    const glm::vec3 toLight = directionLengthSquared > 1.0e-12f
        ? directionToLight / std::sqrt(directionLengthSquared)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    // A light looking straight down cannot use world up as its up vector, because the two would be
    // parallel and `lookAt` would produce a degenerate basis.
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(toLight, up)) > 0.96f) up = glm::vec3(0.0f, 0.0f, 1.0f);
    const float range = std::max(depthRange, 1.0e-3f);
    // The eye sits `range` back along the light axis. A caller sizing `range` to the scene's radius
    // therefore puts everything within that radius inside the orthographic depth band `fitCascade`
    // builds around the slice. Getting this wrong does not fail loudly: geometry outside the depth
    // band simply stops casting and receiving shadows, which reads as a straight-edged seam across
    // the ground rather than as an error.
    return glm::lookAt(pivot + toLight * range, pivot, up);
}

CascadeFit fitCascade(
    const std::array<glm::vec3, 8U>& corners,
    const glm::mat4& lightView,
    float lightViewRange,
    float splitDistance,
    int resolution
) {
    CascadeFit fit;
    fit.splitDistance = splitDistance;

    // The slice's extent is measured in the shared light space, so every cascade's box is expressed
    // in one frame and the transmission and caustics passes can keep using that same frame.
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    for (const glm::vec3& corner : corners) {
        const glm::vec3 inLight = glm::vec3(lightView * glm::vec4(corner, 1.0f));
        minimum = glm::min(minimum, inLight);
        maximum = glm::max(maximum, inLight);
        fit.centreDepth += inLight.z;
    }
    fit.centreDepth /= static_cast<float>(corners.size());

    const int texels = std::max(resolution, 1);
    const float texelSize = 1.0f / static_cast<float>(texels);

    // The box is squared around the slice's centre and rounded up to whole texels. Squaring keeps the
    // extent independent of the sun's azimuth, so rotating the sun about the vertical does not make
    // the resolution breathe; rounding to whole texels is what lets the snap below pin the grid.
    const glm::vec2 centre(
        (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f
    );
    const float halfExtent = std::max(
        std::max(maximum.x - minimum.x, maximum.y - minimum.y) * 0.5f, 1.0e-4f
    );
    const float snappedHalfExtent =
        std::ceil(halfExtent / texelSize) * texelSize;
    fit.texelWorldSize = snappedHalfExtent * 2.0f / static_cast<float>(texels);

    // Snap the centre to the texel grid in light space, which pins the grid to the light rather than
    // letting it slide under the geometry as the camera moves.
    const glm::vec2 snappedCentre(
        snapToTexel(centre.x, fit.texelWorldSize),
        snapToTexel(centre.y, fit.texelWorldSize)
    );

    // The depth band is anchored to the light view rather than to the slice. `buildLightView` puts the
    // eye `range` back along the light axis looking at the pivot, so in light space the geometry
    // around the pivot sits at depth `-range` and the eye is at `0`. The band
    // extends almost to that eye and two ranges behind it, covering casters on
    // either side of the pivot. It expands further for a distant camera slice.
    const float range = std::max(lightViewRange, 1.0e-3f);
    // Positive distances from the eye along the light's -Z viewing axis are
    // the near/far parameters for a right-handed orthographic projection.
    const float sliceFarDistance = -std::min(minimum.z, maximum.z);
    // Casters can lie between the light and the camera slice. Clipping the
    // light volume at the slice's nearest receiver silently drops those
    // casters (notably a cube above an ocean surface).
    const float bandNear = 1.0e-3f;
    const float bandFar = std::max(
        std::max(sliceFarDistance, range * 2.0f), bandNear + 1.0e-3f
    );
    const glm::mat4 lightProjection = glm::ortho(
        snappedCentre.x - snappedHalfExtent, snappedCentre.x + snappedHalfExtent,
        snappedCentre.y - snappedHalfExtent, snappedCentre.y + snappedHalfExtent,
        bandNear, bandFar
    );
    fit.lightViewProjection = lightProjection * lightView;
    return fit;
}

} // namespace shadow
