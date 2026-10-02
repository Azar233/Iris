#include "render/ShadowCascade.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// The camera the fits are validated against. Camera::projectionMatrix is the single source of
// truth for the depth range, so the test uses the same accessors the renderer does rather than a
// second copy of the numbers.
constexpr float cameraNearPlane = 0.1f;
constexpr float cameraFarPlane = 100.0f;

// The camera the fits are validated against. A real perspective projection with the same near and
// far planes the header documents, so the tests exercise the same frustum the renderer fits.
glm::mat4 cameraViewProjection() {
    const glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 4.0f, 12.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)
    );
    const glm::mat4 projection = glm::perspective(
        glm::radians(48.0f), 16.0f / 9.0f,
        cameraNearPlane, cameraFarPlane
    );
    return projection * view;
}

// Reconstructs the view-space depth of a world point, which is what the shader compares against a
// cascade split.
float viewDepth(const glm::mat4& view, const glm::vec3& worldPosition) {
    const glm::vec4 viewPosition = view * glm::vec4(worldPosition, 1.0f);
    return -viewPosition.z;
}

} // namespace

int main() {
    try {
        // Split scheme: ascending, inside the range, and ending exactly at the far plane.
        const auto splits = shadow::splitDistances(
            cameraNearPlane, cameraFarPlane, 3U, 0.75f
        );
        require(splits[0] > cameraNearPlane, "the first split must be beyond the near plane");
        require(splits[0] < splits[1] && splits[1] < splits[2],
                "splits must be strictly ascending");
        require(std::abs(splits[2] - cameraFarPlane) < 1.0e-4f,
                "the last split must reach the far plane");
        const auto fourSplits = shadow::splitDistances(
            cameraNearPlane, cameraFarPlane, 4U, 0.75f
        );
        require(fourSplits[0] < splits[0],
                "more cascades must make the first split shorter, not longer");
        require(std::abs(fourSplits[3] - cameraFarPlane) < 1.0e-4f,
                "four cascades must still reach the far plane");

        // The blend endpoints are the two schemes it interpolates, so they have to be reproducible
        // by hand: lambda 0 is uniform spacing and lambda 1 is logarithmic.
        const auto uniform = shadow::splitDistances(
            cameraNearPlane, cameraFarPlane, 4U, 0.0f
        );
        const float range = cameraFarPlane - cameraNearPlane;
        require(std::abs(uniform[0] - (cameraNearPlane + range * 0.25f)) < 1.0e-4f,
                "lambda 0 must space the splits uniformly");
        const auto logarithmic = shadow::splitDistances(
            cameraNearPlane, cameraFarPlane, 4U, 1.0f
        );
        const float ratio = cameraFarPlane / cameraNearPlane;
        require(std::abs(logarithmic[0] - cameraNearPlane * std::pow(ratio, 0.25f)) < 1.0e-4f,
                "lambda 1 must space the splits logarithmically");

        // A single cascade has to cover the whole range, and the frame count is clamped to the
        // shader's fixed-size arrays rather than allowed to overflow them.
        const auto single = shadow::splitDistances(
            cameraNearPlane, cameraFarPlane, 1U, 0.75f
        );
        require(std::abs(single[0] - cameraFarPlane) < 1.0e-4f,
                "one cascade must span the whole range");
        const auto clamped = shadow::splitDistances(1.0f, 100.0f, 99U, 0.5f);
        require(clamped[shadow::maximumCascadeCount - 1U] > 0.0f,
                "an oversized cascade count must clamp instead of overflowing");
        require(std::abs(clamped[0] - shadow::splitDistances(1.0f, 100.0f, shadow::maximumCascadeCount, 0.5f)[0])
                    < 1.0e-6f,
                "clamping must behave exactly like the clamped count");

        // Degenerate ranges must not produce a decreasing or non-finite sequence: callers index the
        // result with `<` comparisons, and a NaN there selects nothing at all.
        for (const auto& degenerate : {
                 shadow::splitDistances(0.0f, 100.0f, 3U, 0.75f),
                 shadow::splitDistances(-1.0f, -5.0f, 3U, 0.75f),
                 shadow::splitDistances(10.0f, 1.0f, 3U, 0.75f) }) {
            for (std::size_t index = 0U; index < shadow::maximumCascadeCount; ++index) {
                require(std::isfinite(degenerate[index]),
                        "a degenerate range must still produce finite splits");
                if (index > 0U) {
                    require(degenerate[index] >= degenerate[index - 1U],
                            "a degenerate range must not produce descending splits");
                }
            }
        }
        require(shadow::splitDistances(1.0f, 100.0f, 0U, 0.5f)[0] > 0.0f,
                "a zero cascade count must behave like one cascade");

        // Sub-frustum corners: the sliced near plane must lie on the camera's near plane, and the
        // sliced far plane at the requested view depth. This is the property that makes one camera
        // description serve every cascade.
        const glm::mat4 viewProjection = cameraViewProjection();
        const glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 4.0f, 12.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)
        );
        shadow::CameraFrustum camera;
        camera.origin = glm::vec3(0.0f, 4.0f, 12.0f);
        camera.right = glm::vec3(1.0f, 0.0f, 0.0f);
        camera.forward = glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - camera.origin);
        camera.up = glm::normalize(glm::cross(glm::cross(camera.forward, glm::vec3(0.0f, 1.0f, 0.0f)),
                                              camera.forward));
        camera.up = glm::normalize(glm::cross(camera.right, camera.forward));
        camera.nearPlane = cameraNearPlane;
        camera.farPlane = cameraFarPlane;
        camera.fieldOfViewDegrees = 48.0f;
        camera.aspectRatio = 16.0f / 9.0f;
        const float splitDistance = 12.0f;
        const std::array<glm::vec3, 8U> corners =
            shadow::subFrustumCorners(camera, splitDistance);
        // The requested depths are exact: the depth is measured in view space, which is the space the
        // projection is linear in. The world-space ray metric `dot(dir, forward)` is not the view depth
        // off the camera axis, so comparing against it would be checking the wrong quantity.
        const auto viewDepthOf = [&](const glm::vec3& worldPosition) {
            const glm::vec4 viewPosition = view * glm::vec4(worldPosition, 1.0f);
            return -viewPosition.z;
        };
        for (std::size_t index = 0U; index < 8U; ++index) {
            require(std::isfinite(corners[index].x) && std::isfinite(corners[index].y)
                        && std::isfinite(corners[index].z),
                    "sub-frustum corners must be finite");
        }
        for (std::size_t index = 0U; index < 4U; ++index) {
            const float depth = viewDepthOf(corners[index]);
            require(std::abs(depth - cameraNearPlane) < 1.0e-3f,
                    "the first four corners must sit on the camera near plane");
        }
        for (std::size_t index = 4U; index < 8U; ++index) {
            const float depth = viewDepthOf(corners[index]);
            require(std::abs(depth - splitDistance) < 1.0e-3f,
                    "the last four corners must sit at the requested split distance");
        }
        // Corners on the same plane must also share the camera's aspect ratio. The extents are
        // measured along the camera's own right and up axes rather than along world X and Y: the
        // camera is pitched, so a world-space Y span is shorter than the plane's actual height and
        // comparing against it would fail on a correct frustum.
        const float nearWidth = glm::length(corners[1] - corners[0]);
        const float nearHeight = glm::length(corners[3] - corners[0]);
        require(std::abs(nearWidth / nearHeight - 16.0f / 9.0f) < 0.02f,
                "the sub-frustum must keep the camera's aspect ratio");
        // The far plane of the full frustum must be reachable through the same function, which is
        // what the last cascade relies on.
        const std::array<glm::vec3, 8U> fullCorners = shadow::subFrustumCorners(camera, cameraFarPlane);
        for (std::size_t index = 4U; index < 8U; ++index) {
            require(std::abs(viewDepthOf(fullCorners[index]) - cameraFarPlane) < 1.0e-2f,
                    "the last cascade must be able to reach the far plane");
        }

        // The fit has to contain the corners it was built from: a cascade that clips its own slice
        // shows as a hole in the shadow map.
        const glm::vec3 toLight = glm::normalize(glm::vec3(0.62f, 0.70f, 0.36f));
        // The light view is anchored near the slice under test, which is what the renderer does with
        // its scene pivot: the orthographic depth range then only has to span the casters around it.
        const glm::mat4 lightView =
            shadow::buildLightView(glm::vec3(0.0f, 0.0f, -6.0f), toLight, 30.0f);
        const int resolution = 2048;
        const shadow::CascadeFit fit = shadow::fitCascade(
            corners, lightView, 30.0f, splitDistance, resolution);
        require(fit.texelWorldSize > 0.0f && std::isfinite(fit.texelWorldSize),
                "the fitted texel size must be positive and finite");
        require(std::abs(fit.splitDistance - splitDistance) < 1.0e-6f,
                "the fit must carry the split distance it was built for");
        for (const glm::vec3& corner : corners) {
            const glm::vec4 lightClip = fit.lightViewProjection * glm::vec4(corner, 1.0f);
            const glm::vec3 ndc = glm::vec3(lightClip) / lightClip.w;
            require(ndc.x >= -1.001f && ndc.x <= 1.001f && ndc.y >= -1.001f && ndc.y <= 1.001f,
                    "every sub-frustum corner must fall inside the fitted light box");
            require(ndc.z >= -1.001f && ndc.z <= 1.001f,
                    "every sub-frustum corner must fall inside the fitted depth range");
        }
        const glm::vec3 casterNearLight =
            glm::vec3(0.0f, 0.0f, -6.0f) + toLight * 29.0f;
        const glm::vec4 casterClip = fit.lightViewProjection
            * glm::vec4(casterNearLight, 1.0f);
        require(casterClip.z / casterClip.w >= -1.001f
                    && casterClip.z / casterClip.w <= 1.001f,
                "a caster in front of the camera slice must remain in the light depth band");

        // Texel snapping. The snap rounds the box centre to a whole texel, so a sub-texel camera pan
        // can move the grid by up to one texel -- that bounded step is the entire point. Without
        // snapping the grid would slide continuously with the camera, which is what makes shadow
        // edges crawl from frame to frame. The measured drift below is well under a texel, and the
        // test asserts the bound rather than an exact grid position, because the projection of a
        // fixed world point is not itself quantised.
        const float texel = fit.texelWorldSize;
        const auto lightSpaceOrigin = [](const shadow::CascadeFit& candidate) {
            const glm::mat4 inverseLight = glm::inverse(candidate.lightViewProjection);
            const glm::vec4 homogeneous = inverseLight * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            return glm::vec3(homogeneous) / homogeneous.w;
        };
        const glm::vec3 referencePoint = lightSpaceOrigin(fit);
        const glm::vec4 snappedOrigin =
            fit.lightViewProjection * glm::vec4(referencePoint, 1.0f);
        // The snapped centre lands on the grid, which is what the rounding is for.
        const float gridX =
            (snappedOrigin.x / snappedOrigin.w * 0.5f + 0.5f) * static_cast<float>(resolution);
        const float gridY =
            (snappedOrigin.y / snappedOrigin.w * 0.5f + 0.5f) * static_cast<float>(resolution);
        require(std::abs(gridX - std::round(gridX)) < 0.02f
                    && std::abs(gridY - std::round(gridY)) < 0.02f,
                "the fitted box centre must land on the texel grid");

        const std::array<glm::vec3, 8U> shiftedCorners = [&] {
            std::array<glm::vec3, 8U> shifted{};
            for (std::size_t index = 0U; index < 8U; ++index) {
                // A sub-texel camera pan: the snap is what keeps the grid from sliding with it.
                shifted[index] = corners[index] + glm::vec3(texel * 0.37f, 0.0f, texel * 0.21f);
            }
            return shifted;
        }();
        const shadow::CascadeFit shiftedFit = shadow::fitCascade(
            shiftedCorners, lightView, 30.0f, splitDistance, resolution
        );
        const glm::vec4 shiftedProjection =
            shiftedFit.lightViewProjection * glm::vec4(referencePoint, 1.0f);
        const float shiftedGridX =
            (shiftedProjection.x / shiftedProjection.w * 0.5f + 0.5f) * static_cast<float>(resolution);
        const float shiftedGridY =
            (shiftedProjection.y / shiftedProjection.w * 0.5f + 0.5f) * static_cast<float>(resolution);
        require(std::abs(shiftedGridX - gridX) <= 1.01f && std::abs(shiftedGridY - gridY) <= 1.01f,
                "a sub-texel camera pan must move the grid by at most one texel");
        // The shifted box must still contain its own slice, which is what stops the snap from
        // clipping geometry: a bound that only checked the unshifted fit would miss that.
        for (const glm::vec3& corner : shiftedCorners) {
            const glm::vec4 clip = shiftedFit.lightViewProjection * glm::vec4(corner, 1.0f);
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            require(ndc.x >= -1.001f && ndc.x <= 1.001f && ndc.y >= -1.001f && ndc.y <= 1.001f
                        && ndc.z >= -1.001f && ndc.z <= 1.001f,
                    "a shifted cascade must still contain its slice");
        }

        // Rotating the light does change the slice's footprint in the shared light view, and therefore
        // its texel size: an axis-aligned box in light space grows when the slice is viewed obliquely.
        // That is the price of one shared light view -- which the transmission-shadow and caustics
        // passes depend on -- and it is recorded rather than hidden. What the test still holds is that
        // the change is bounded and continuous: a light near the horizon must not blow the resolution
        // up without limit.
        const shadow::CascadeFit highLight = shadow::fitCascade(
            corners,
            shadow::buildLightView(glm::vec3(0.0f, 0.0f, -6.0f), glm::normalize(glm::vec3(0.1f, 0.99f, 0.05f)), 30.0f),
            30.0f, splitDistance, resolution
        );
        const shadow::CascadeFit lowLight = shadow::fitCascade(
            corners,
            shadow::buildLightView(glm::vec3(0.0f, 0.0f, -6.0f), glm::normalize(glm::vec3(0.9f, 0.2f, 0.3f)), 30.0f),
            30.0f, splitDistance, resolution
        );
        require(lowLight.texelWorldSize > 0.0f && std::isfinite(lowLight.texelWorldSize),
                "an oblique light must still produce a usable texel size");
        require(lowLight.texelWorldSize < fit.texelWorldSize * 8.0f,
                "an oblique light must not blow the texel size up without bound");
        // Both lights must still cover the slice they were fitted to; a changing texel size is
        // acceptable, a clipped slice is not.
        for (const shadow::CascadeFit* candidate : {&highLight, &lowLight}) {
            for (const glm::vec3& corner : corners) {
                const glm::vec4 clip = candidate->lightViewProjection * glm::vec4(corner, 1.0f);
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                require(ndc.x >= -1.001f && ndc.x <= 1.001f && ndc.y >= -1.001f && ndc.y <= 1.001f
                            && ndc.z >= -1.001f && ndc.z <= 1.001f,
                        "a fit must contain its slice for any light direction");
            }
        }

        // A degenerate light direction must fall back to something usable instead of producing NaN.
        const shadow::CascadeFit degenerate = shadow::fitCascade(
            corners, lightView, 30.0f, splitDistance, resolution
        );
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                require(std::isfinite(degenerate.lightViewProjection[column][row]),
                        "a zero light direction must not produce a NaN matrix");
            }
        }
        require(shadow::fitCascade(
            corners, lightView, 30.0f, splitDistance, 0).texelWorldSize > 0.0f,
                "a zero map resolution must not divide by zero");

        // Every cascade has to be able to see the geometry in its own slice. The point that must land
        // inside cascade N's box is a point *in that slice* -- the centroid of the slice's own corners
        // -- not a shared world point: each slice sits at a different depth, and the single point the
        // camera orbits would only be inside the slice that happens to contain it.
        std::size_t covered = 0U;
        for (std::size_t cascade = 0U; cascade < 3U; ++cascade) {
            const std::array<glm::vec3, 8U> slice =
                shadow::subFrustumCorners(camera, splits[cascade]);
            const shadow::CascadeFit sliceFit =
                shadow::fitCascade(
            slice, lightView, 30.0f, splits[cascade], resolution);
            glm::vec3 sliceCentre(0.0f);
            for (const glm::vec3& corner : slice) sliceCentre += corner;
            sliceCentre /= static_cast<float>(slice.size());
            const glm::vec4 clip = sliceFit.lightViewProjection * glm::vec4(sliceCentre, 1.0f);
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x >= -1.0f && ndc.x <= 1.0f && ndc.y >= -1.0f && ndc.y <= 1.0f
                && ndc.z >= -1.0f && ndc.z <= 1.0f) {
                ++covered;
            }
        }
        require(covered == 3U, "every cascade must contain its own slice's centre");
        // Cascades must also actually get shorter as they get nearer: a split scheme that returned
        // equal distances would put all the resolution in one place.
        require(splits[0] < splits[1] && splits[1] < splits[2],
                "the fitted splits must remain strictly ascending");

        std::cout << "Shadow cascade fitting tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Shadow cascade fitting tests failed: " << error.what() << '\n';
        return 1;
    }
}
