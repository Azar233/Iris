#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct TransparentSortEntry {
    std::size_t renderItemIndex{0};
    std::size_t transparentSubmeshIndex{0};
    glm::vec3 worldCenter{0.0f};
};

void sortTransparentBackToFront(
    std::vector<TransparentSortEntry>& entries,
    const glm::vec3& cameraPosition
);

struct BoundingSphere {
    glm::vec3 center{0.0f};
    float radius{0.0f};
};

struct ViewFrustum {
    std::array<glm::vec4, 6> planes{};
};

ViewFrustum extractViewFrustum(const glm::mat4& viewProjection);
BoundingSphere transformBoundingSphere(
    const BoundingSphere& localBounds,
    const glm::mat4& modelMatrix
);
bool intersectsViewFrustum(const ViewFrustum& frustum, const BoundingSphere& bounds);
float projectedSphereRadiusPixels(
    const BoundingSphere& bounds,
    const glm::vec3& cameraPosition,
    float verticalFieldOfViewRadians,
    int viewportHeight
);
std::size_t selectLodLevel(
    float projectedRadiusPixels,
    float mediumThresholdPixels,
    float highThresholdPixels
);

// Clip-space AABB support test. Invalid bounds/matrices remain visible.
// Transform the planes to local space so mirrored, nonuniform and sheared
// transforms remain conservative without a world-space bounding sphere.
struct BoundingBox {
    glm::vec3 minimum{1.0f};
    glm::vec3 maximum{-1.0f};
};
struct RasterVisibilityStatistics {
    std::uint64_t submittedDraws{0}, culledDraws{0};
    std::uint64_t submittedTriangles{0}, drawnTriangles{0};
};
inline bool intersectsClipBox(const BoundingBox& bounds, const glm::mat4& localToClip) {
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(bounds.minimum[i]) || !std::isfinite(bounds.maximum[i])
            || bounds.minimum[i] > bounds.maximum[i]) return true;
    }
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        if (!std::isfinite(localToClip[c][r])) return true;
    const glm::vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
    const glm::vec3 extent = (bounds.maximum - bounds.minimum) * 0.5f;
    const auto row = [&](int r) { return glm::vec4(localToClip[0][r], localToClip[1][r],
                                                 localToClip[2][r], localToClip[3][r]); };
    const auto x = row(0), y = row(1), z = row(2), w = row(3);
    const std::array<glm::vec4, 6> planes{{w+x, w-x, w+y, w-y, w+z, w-z}};
    for (const auto& plane : planes) {
        const float distance = glm::dot(glm::vec3(plane), center);
        const float radius = glm::dot(glm::abs(glm::vec3(plane)), extent);
        const float tolerance = 1e-4f * (std::abs(distance) + radius + std::abs(plane.w) + 1.0f);
        if (distance + plane.w + radius < -tolerance) return false;
    }
    return true;
}
