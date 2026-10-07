// Cloud layer measurement tool.
//
// Prints, for one scene's atmosphere and cloud parameters, the sky radiance and the cloud layer's
// scattered radiance across the elevations a ground-level camera actually sees, plus the ratio
// between them. That ratio is the whole reason this tool exists: whether a cloud reads as a cloud,
// as a black stain or as a hole in the sky is decided by whether it is brighter or darker than the
// sky it covers, and that is a number, not an opinion. Calibrating the layer by editing images
// repeatedly does not converge; calibrating it from this table does.
//
// The swept values are the layer's two lighting parameters, `cloudAmbientElevationDegrees` and
// `cloudAmbientScale`. The shipped defaults come from the `ambientElevation=15, scale=0.85` row.
#include "optics/Atmosphere.h"
#include "optics/CloudReference.h"
#include "scene/SceneDocument.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <queue>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------- shape metrics
//
// Whether a cloud layer reads as *clouds* or as an even texture is the one property this project
// kept failing to pin down: C1 produced a fine speckle, C2's march produced a finer one, and both
// times the only available judge was a person looking at a frame. That is not a contract, and
// "looks grainy" is not something a regression can hold.
//
// These metrics make shape measurable on the CPU reference -- the same model the GPU runs, which is
// what makes measuring it here meaningful:
//   - `coverage` is the fraction of the frame the layer hides. Too high and the sky is overcast.
//   - `components` counts the distinct covered regions. A layer of clouds has several; a uniform
//     texture that happens to be patchy has one connected blob with a fractal edge.
//   - `largest` is the biggest region as a fraction of the covered area. One region covering almost
//     everything means there are no clouds, only weather.
//   - `edgeLength` totals the mask's boundary pixels: a high count against a low coverage is the
//     signature of speckle, because a smooth blob has a short perimeter for its area.
//   - `edgeDensity` is therefore `edgeLength / covered`, the number to compare across parameters.
struct ShapeMetrics {
    double coverage{0.0};
    int components{0};
    double largest{0.0};
    double edgeDensity{0.0};
    // Mean density integrated with a fixed 64-sample quadrature, independent of quality tier.
    double meanDensity{0.0};
};

// Renders a small CPU image of the layer and measures its shape. The frame looks up at a fixed
// elevation band, so the numbers describe the layer rather than the horizon.
ShapeMetrics measureShape(
    const atmosphere::AtmosphereParameters& parameters,
    const cloud::MarchSettings& settings,
    int width,
    int height,
    std::vector<float>* image = nullptr,
    const Camera* camera = nullptr
) {
    std::vector<float> transmittance(static_cast<std::size_t>(width) * height, 1.0f);
    double densitySum = 0.0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
            // A 40-degree band centred on 35 degrees elevation: high enough that the slab is crossed
            // rather than grazed, which keeps the comparison about the field and not the projection.
            const float elevation = glm::radians(15.0f + v * 40.0f);
            const float azimuth = -1.2f + u * 2.4f;
            glm::vec3 direction(
                std::cos(elevation) * std::sin(azimuth),
                std::sin(elevation),
                std::cos(elevation) * std::cos(azimuth));
            glm::vec3 origin(0.0f, 1.1f, 0.0f);
            if (camera != nullptr) {
                const float halfHeight = std::tan(glm::radians(camera->fieldOfView()) * 0.5f);
                direction = glm::normalize(camera->forwardDirection()
                    + camera->rightDirection() * ((u * 2.0f - 1.0f) * halfHeight * width / height)
                    + camera->upDirection() * ((v * 2.0f - 1.0f) * halfHeight));
                origin = camera->position();
            }
            transmittance[static_cast<std::size_t>(y) * width + x] =
                cloud::march(origin, direction, parameters, settings).transmittance;
            const cloud::SlabSpan span = cloud::slabSpan(origin.y, direction.y,
                parameters.cloudBaseHeight, parameters.cloudTopHeight);
            if (span.valid) {
                double columnDensity = 0.0;
                for (int sample = 0; sample < 64; ++sample) {
                    columnDensity += cloud::densityAt(origin + direction *
                        (span.start + (static_cast<float>(sample) + 0.5f) * span.length / 64.0f),
                        parameters);
                }
                densitySum += columnDensity / 64.0;
            }
        }
    }

    if (image != nullptr) *image = transmittance;
    // A cell counts as covered when the layer hides more than half of what is behind it. The
    // half-transmittance contour is the layer's silhouette.
    std::vector<unsigned char> covered(transmittance.size(), 0U);
    std::size_t coveredCount = 0U;
    for (std::size_t index = 0U; index < transmittance.size(); ++index) {
        if (transmittance[index] < 0.5f) {
            covered[index] = 1U;
            ++coveredCount;
        }
    }
    ShapeMetrics metrics;
    const double total = static_cast<double>(transmittance.size());
    metrics.coverage = static_cast<double>(coveredCount) / total;
    metrics.meanDensity = densitySum / total;

    std::size_t edgeCount = 0U;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            if (covered[index] == 0U) continue;
            const bool boundaryNeighbour =
                (x == 0 || covered[index - 1U] == 0U)
                || (x == width - 1 || covered[index + 1U] == 0U)
                || (y == 0 || covered[index - static_cast<std::size_t>(width)] == 0U)
                || (y == height - 1 || covered[index + static_cast<std::size_t>(width)] == 0U);
            if (boundaryNeighbour) ++edgeCount;
        }
    }
    if (coveredCount > 0U) {
        metrics.edgeDensity = static_cast<double>(edgeCount) / static_cast<double>(coveredCount);
    }

    // Four-connected components over the covered mask.
    std::vector<unsigned char> visited(covered.size(), 0U);
    std::size_t largestComponent = 0U;
    for (int start = 0; start < static_cast<int>(covered.size()); ++start) {
        if (covered[static_cast<std::size_t>(start)] == 0U
            || visited[static_cast<std::size_t>(start)] != 0U) {
            continue;
        }
        std::size_t size = 0U;
        std::queue<int> pending;
        pending.push(start);
        visited[static_cast<std::size_t>(start)] = 1U;
        while (!pending.empty()) {
            const int current = pending.front();
            pending.pop();
            ++size;
            const int cx = current % width;
            const int cy = current / width;
            const int neighbours[4][2] = {{cx - 1, cy}, {cx + 1, cy}, {cx, cy - 1}, {cx, cy + 1}};
            for (const auto& neighbour : neighbours) {
                if (neighbour[0] < 0 || neighbour[0] >= width) continue;
                if (neighbour[1] < 0 || neighbour[1] >= height) continue;
                const int index = neighbour[1] * width + neighbour[0];
                if (covered[static_cast<std::size_t>(index)] == 0U
                    || visited[static_cast<std::size_t>(index)] != 0U) {
                    continue;
                }
                visited[static_cast<std::size_t>(index)] = 1U;
                pending.push(index);
            }
        }
        ++metrics.components;
        largestComponent = std::max(largestComponent, size);
    }
    metrics.largest = coveredCount > 0U
        ? static_cast<double>(largestComponent) / static_cast<double>(coveredCount) : 0.0;
    return metrics;
}

double luminance(const glm::vec3& value) {
    return 0.2126 * value.r + 0.7152 * value.g + 0.0722 * value.b;
}

constexpr float pi = 3.14159265358979323846f;

// The scene's world unit is about 133 metres, so these heights are the physical ones in disguise.
struct Case {
    float base;
    float top;
    float feature;
    float coverage;
    float density;
};

void evaluate(const Case& c) {
    atmosphere::AtmosphereParameters p;
    p.enabled = true;
    p.sunElevationDegrees = 45.0f;
    p.sunAzimuthDegrees = 136.0f;
    p.skyIntensity = 3.0f;
    p.sunIntensity = 3.0f;
    p.groundAlbedo = 0.25f;
    p.cloudsEnabled = true;
    p.cloudBaseHeight = c.base;
    p.cloudTopHeight = c.top;
    p.cloudFeatureScale = c.feature;
    p.cloudCoverage = c.coverage;
    p.cloudDensity = c.density;

    const float eye = 1.1f;
    std::printf("base=%-6.0f top=%-6.0f feature=%-6.0f coverage=%.2f density=%.2f | ",
        c.base, c.top, c.feature, c.coverage, c.density);
    // The visible band a 48-degree vertical field of view covers when the camera is level.
    for (float elevation : {5.0f, 15.0f, 30.0f, 45.0f}) {
        const float e = elevation * pi / 180.0f;
        double covered = 0.0;
        double ratioSum = 0.0;
        int coveredCount = 0;
        const int samples = 72;
        for (int i = 0; i < samples; ++i) {
            const float a = 2.0f * pi * static_cast<float>(i) / static_cast<float>(samples);
            const glm::vec3 dir(std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a));
            const glm::vec3 sky = atmosphere::skyRadiance(dir, p);
            const atmosphere::CloudLayer layer = atmosphere::cloudLayer(dir, p, eye);
            if (layer.mask > 0.05f) {
                covered += 1.0;
                ratioSum += luminance(layer.ambient) / std::max(luminance(sky), 1.0e-6);
                ++coveredCount;
            }
        }
        const double ratio = coveredCount > 0 ? ratioSum / coveredCount : 0.0;
        std::printf("%2.0fdeg:%3.0f%% r=%.2f  ", elevation,
            100.0 * covered / samples, ratio);
    }
    std::printf("\n");
}

// Sweep the ambient source elevation and its coefficient, which is what decides whether a cloud
// reads as brighter or darker than the sky it covers.
void sweepAmbient(float base, float top, float feature, float coverage) {
    std::printf("--- ambient sweep (base %.0f top %.0f feature %.0f coverage %.2f) ---\n",
        base, top, feature, coverage);
    for (float ambientElevation : {0.0f, 15.0f, 35.0f}) {
        for (float scale : {0.25f, 0.5f, 0.85f}) {
            atmosphere::AtmosphereParameters p;
            p.enabled = true;
            p.sunElevationDegrees = 45.0f;
            p.sunAzimuthDegrees = 136.0f;
            p.skyIntensity = 3.0f;
            p.sunIntensity = 3.0f;
            p.groundAlbedo = 0.25f;
            p.cloudsEnabled = true;
            p.cloudBaseHeight = base;
            p.cloudTopHeight = top;
            p.cloudFeatureScale = feature;
            p.cloudCoverage = coverage;
            p.cloudAmbientElevationDegrees = ambientElevation;
            p.cloudAmbientScale = scale;

            const float eye = 1.1f;
            std::printf("  ambElev=%-5.0f scale=%.2f | ", ambientElevation, scale);
            for (float elevation : {5.0f, 15.0f, 30.0f}) {
                const float e = elevation * pi / 180.0f;
                double ratioSum = 0.0;
                int count = 0;
                for (int i = 0; i < 72; ++i) {
                    const float a = 2.0f * pi * static_cast<float>(i) / 72.0f;
                    const glm::vec3 dir(std::cos(e) * std::sin(a), std::sin(e),
                        std::cos(e) * std::cos(a));
                    const glm::vec3 sky = atmosphere::skyRadiance(dir, p);
                    const atmosphere::CloudLayer layer = atmosphere::cloudLayer(dir, p, eye);
                    if (layer.mask > 0.05f) {
                        ratioSum += luminance(layer.ambient) / std::max(luminance(sky), 1.0e-6);
                        ++count;
                    }
                }
                std::printf("%2.0fdeg r=%.2f  ", elevation,
                    count > 0 ? ratioSum / count : 0.0);
            }
            std::printf("\n");
        }
    }
}
// The volume march's own calibration.
//
// C1's coefficients were chosen for a *single* density sample per direction. A march accumulates
// dozens of them, each weighted by `1 - exp(-density * step * extinction)`, so the same ambient and
// sun radiances produce a far brighter result -- which is exactly what happened: the first frame with
// the march switched on blew the sky out to white.
//
// Two quantities decide it, and they are not interchangeable:
//   - `extinction` sets the cloud's *opacity*. It has to be large enough that a vertical ray through
//     the slab reaches an optical depth of several, or the layer is a translucent haze no matter how
//     the light is scaled.
//   - `ambientScale` / `sunScale` set its *brightness*. The sun term carries the phase function,
//     whose forward peak is ~23, so it is scaled well below the ambient term or the silver lining
//     becomes a hole burned through the layer.
void sweepMarch(float base, float top, float feature, float coverage) {
    std::printf("--- volume march sweep (base %.0f top %.0f feature %.0f coverage %.2f) ---\n",
        base, top, feature, coverage);
    std::printf("%-9s %-11s %-9s | %s\n", "extinction", "ambient", "sun", "cloud/sky at 5 / 15 / 30 / 45 deg");

    atmosphere::AtmosphereParameters baseParameters;
    baseParameters.enabled = true;
    baseParameters.sunElevationDegrees = 45.0f;
    baseParameters.sunAzimuthDegrees = 136.0f;
    baseParameters.skyIntensity = 3.0f;
    baseParameters.sunIntensity = 3.0f;
    baseParameters.groundAlbedo = 0.25f;
    baseParameters.cloudsEnabled = true;
    baseParameters.cloudBaseHeight = base;
    baseParameters.cloudTopHeight = top;
    baseParameters.cloudFeatureScale = feature;
    baseParameters.cloudCoverage = coverage;

    const glm::vec3 skyAmbient = atmosphere::skyRadiance(
        glm::vec3(0.0f, 1.0f, 0.0f), baseParameters);
    const glm::vec3 skyLight = atmosphere::skyLightColor(baseParameters)
        * std::max(baseParameters.sunIntensity, 0.0f);

    for (float extinction : {0.02f, 0.05f}) {
        for (float ambientScale : {0.5f, 1.0f, 2.0f}) {
            for (float sunScale : {0.05f, 0.15f}) {
                cloud::MarchSettings settings;
                settings.primarySteps = 32;
                settings.lightSteps = 6;
                settings.extinction = extinction;
                settings.jitter = 0.0f;
                settings.sunDirection = atmosphere::sunDirection(baseParameters);
                settings.ambientRadiance = skyAmbient * ambientScale;
                settings.sunRadiance = skyLight * sunScale;

                std::printf("%-9.3f %-11.2f %-9.2f | ", extinction, ambientScale, sunScale);
                for (float elevation : {5.0f, 15.0f, 30.0f, 45.0f}) {
                    const float e = elevation * pi / 180.0f;
                    double ratioSum = 0.0;
                    int count = 0;
                    for (int i = 0; i < 36; ++i) {
                        const float a = 2.0f * pi * static_cast<float>(i) / 36.0f;
                        const glm::vec3 direction(std::cos(e) * std::sin(a), std::sin(e),
                            std::cos(e) * std::cos(a));
                        const cloud::MarchResult marched =
                            cloud::march(glm::vec3(0.0f, 1.1f, 0.0f), direction,
                                baseParameters, settings);
                        // What the viewer sees through the layer, which is the scattered radiance
                        // plus whatever survives of the sky behind it.
                        const glm::vec3 seen = marched.radiance
                            + atmosphere::skyRadiance(direction, baseParameters)
                                * marched.transmittance;
                        ratioSum += luminance(seen)
                            / std::max(luminance(atmosphere::skyRadiance(direction, baseParameters)),
                                1.0e-6);
                        ++count;
                    }
                    std::printf("%5.2f ", count > 0 ? ratioSum / count : 0.0);
                }
                std::printf("\n");
            }
        }
    }
}
} // namespace

// The shape sweep: which parameters make the layer read as clouds rather than as texture.
//
// The numbers are in `ShapeMetrics` above. What to look for in a row:
//   - `edgeDensity` well below 0.5. A speckled mask has a long perimeter for the area it covers; a
//     blob does not. This is the statistic that separates the two, and it is why the sweep exists.
//   - `components` above one and `largest` well below 1, so there are several clouds rather than one
//     connected mass.
//   - `coverage` in a range a viewer would call broken cloud, roughly 0.2 to 0.7.
//
// C2's best row here measured `edgeDensity` 0.369 with 9 components -- every parameter combination
// the field admitted, at every coverage from 0.1 to 0.7, landed between 0.53 and 1.0. That is the
// result that falsified the C2 extinction choice and sent the density construction back to C5.
//
// `meanDensity` is the column extinction is actually chosen from: the average density along the
// marched rays, so `meanDensity * slabThickness * extinction` is roughly the optical depth a
// vertical ray reaches. That product is what "opaque enough to be a cloud" means as a number.
void sweepShape(float base, float top) {
    std::printf("--- shape sweep (base %.0f top %.0f) ---\n", base, top);
    std::printf("%-9s %-8s %-9s | %-9s %-11s %-9s %s\n",
        "extinct", "coverage", "meanDens", "coverage", "components", "largest", "edgeDensity");

    // `density` and `extinction` are the same knob -- multiplying one multiplies the other's optical
    // depth -- so a sweep has to move the coverage with them or it measures one point several times.
    // `coverage` is the independent parameter: it decides how much of the density field sits above
    // the silhouette.
    for (float extinction : {0.0015f, 0.0025f, 0.004f}) {
        for (float coverage : {0.35f, 0.45f, 0.55f, 0.65f}) {
            atmosphere::AtmosphereParameters p;
            p.enabled = true;
            p.sunElevationDegrees = 45.0f;
            p.sunAzimuthDegrees = 136.0f;
            p.skyIntensity = 3.0f;
            p.sunIntensity = 3.0f;
            p.groundAlbedo = 0.25f;
            p.cloudsEnabled = true;
            p.cloudBaseHeight = base;
            p.cloudTopHeight = top;
            p.cloudFeatureScale = 6400.0f;
            p.cloudCoverage = coverage;

            cloud::MarchSettings settings;
            settings.primarySteps = 24;
            settings.lightSteps = 4;
            settings.extinction = extinction;
            settings.sunDirection = atmosphere::sunDirection(p);

            const ShapeMetrics metrics = measureShape(p, settings, 96, 64);
            std::printf("%-9.4f %-8.2f %-9.3f | %-9.3f %-11d %-9.3f %.3f\n",
                extinction, coverage, metrics.meanDensity, metrics.coverage, metrics.components,
                metrics.largest, metrics.edgeDensity);
        }
    }
}

// The detail octaves, which C5 moved out of the silhouette and into the interior.
//
// The row to look for is not the lowest `edgeDensity` -- it is the row where `atEdge` is 0 and the
// coverage matches the `strength=0` control exactly. That is the contract: the fine octaves may
// carve a cloud's inside, and may not decide where its outside is. `edgeDensity` then rises with
// `atEdge`, which is the knob that buys ragged edges back at a measurable price instead of by
// accident.
void sweepDetail(float base, float top, float coverage) {
    std::printf("--- detail sweep (base %.0f top %.0f coverage %.2f) ---\n", base, top, coverage);
    std::printf("%-9s %-8s %-9s | %-9s %-11s %-9s %s\n",
        "strength", "atEdge", "meanDens", "coverage", "components", "largest", "edgeDensity");
    for (float strength : {0.0f, 0.45f, 0.9f}) {
        for (float edge : {0.0f, 0.15f, 0.5f, 1.0f}) {
            atmosphere::AtmosphereParameters p;
            p.enabled = true;
            p.sunElevationDegrees = 45.0f;
            p.sunAzimuthDegrees = 136.0f;
            p.skyIntensity = 3.0f;
            p.sunIntensity = 3.0f;
            p.groundAlbedo = 0.25f;
            p.cloudsEnabled = true;
            p.cloudBaseHeight = base;
            p.cloudTopHeight = top;
            p.cloudFeatureScale = 6400.0f;
            p.cloudCoverage = coverage;
            p.cloudDetailStrength = strength;
            p.cloudDetailEdge = edge;

            cloud::MarchSettings settings;
            settings.primarySteps = 24;
            settings.lightSteps = 4;
            settings.extinction = cloud::volumetricExtinction;
            settings.sunDirection = atmosphere::sunDirection(p);

            const ShapeMetrics metrics = measureShape(p, settings, 96, 64);
            std::printf("%-9.2f %-8.2f %-9.3f | %-9.3f %-11d %-9.3f %.3f\n",
                strength, edge, metrics.meanDensity, metrics.coverage, metrics.components,
                metrics.largest, metrics.edgeDensity);
        }
    }
}

// The weather map. `variation=0` is the control: the same field with the map switched off, which is
// what C2 shipped and what the map has to be measurably better than. What to look for is not a
// different `coverage` -- the map is centred on 0.5, so it redistributes coverage rather than adding
// it -- but fewer, larger regions at the same coverage.
void sweepWeather(float base, float top) {
    std::printf("--- weather map sweep (base %.0f top %.0f coverage 0.50) ---\n", base, top);
    std::printf("%-10s %-9s %-9s | %-9s %-11s %-9s %s\n",
        "variation", "typeVar", "meanDens", "coverage", "components", "largest", "edgeDensity");
    for (float variation : {0.0f, 0.45f, 0.90f}) {
        for (float typeVariation : {0.0f, 0.50f}) {
            atmosphere::AtmosphereParameters p;
            p.enabled = true;
            p.sunElevationDegrees = 45.0f;
            p.sunAzimuthDegrees = 136.0f;
            p.skyIntensity = 3.0f;
            p.sunIntensity = 3.0f;
            p.groundAlbedo = 0.25f;
            p.cloudsEnabled = true;
            p.cloudBaseHeight = base;
            p.cloudTopHeight = top;
            p.cloudFeatureScale = 6400.0f;
            p.cloudCoverage = 0.50f;
            p.cloudCoverageVariation = variation;
            p.cloudTypeVariation = typeVariation;

            cloud::MarchSettings settings;
            settings.primarySteps = 24;
            settings.lightSteps = 4;
            settings.extinction = cloud::volumetricExtinction;
            settings.sunDirection = atmosphere::sunDirection(p);

            const ShapeMetrics metrics = measureShape(p, settings, 96, 64);
            std::printf("%-10.2f %-9.2f %-9.3f | %-9.3f %-11d %-9.3f %.3f\n",
                variation, typeVariation, metrics.meanDensity, metrics.coverage, metrics.components,
                metrics.largest, metrics.edgeDensity);
        }
    }
}

// The quality tier. The contract is that the tier changes how much of the integral is paid for and
// nothing else, so the two rows have to describe the same cloud: `coverage` and `components` should
// agree closely, and only `meanDensity` -- a quadrature of the same function at two step counts --
// is allowed to drift.
void sweepTier(float base, float top) {
    std::printf("--- quality tier (base %.0f top %.0f) ---\n", base, top);
    std::printf("%-6s %-7s %-7s %-9s | %-9s %-11s %-9s %s\n",
        "tier", "steps", "lights", "meanDens", "coverage", "components", "largest", "edgeDensity");
    for (atmosphere::CloudQualityTier tier : {
            atmosphere::CloudQualityTier::Low, atmosphere::CloudQualityTier::High}) {
        atmosphere::AtmosphereParameters p;
        p.enabled = true;
        p.sunElevationDegrees = 45.0f;
        p.sunAzimuthDegrees = 136.0f;
        p.skyIntensity = 3.0f;
        p.sunIntensity = 3.0f;
        p.groundAlbedo = 0.25f;
        atmosphere::applyCloudPreset(p, atmosphere::CloudPreset::Cumulus);
        p.cloudQuality = tier;

        const atmosphere::CloudTierBudget budget = atmosphere::cloudTierBudget(tier);
        cloud::MarchSettings settings;
        settings.primarySteps = budget.primarySteps;
        settings.lightSteps = budget.lightSteps;
        settings.extinction = cloud::volumetricExtinction;
        settings.sunDirection = atmosphere::sunDirection(p);

        const ShapeMetrics metrics = measureShape(p, settings, 96, 64);
        std::printf("%-6s %-7d %-7d %-9.3f | %-9.3f %-11d %-9.3f %.3f\n",
            tier == atmosphere::CloudQualityTier::High ? "high" : "low", budget.primarySteps,
            budget.lightSteps, metrics.meanDensity, metrics.coverage, metrics.components,
            metrics.largest, metrics.edgeDensity);
    }
}

// The three cloud type presets, measured rather than described. This is the table the presets'
// numbers were chosen from: a preset is only worth shipping if its row says something different
// from the other two, and if the difference is the one the name claims.
void evaluatePresets() {
    std::printf("--- cloud type presets ---\n");
    std::printf("%-8s %-7s %-7s %-6s %-8s %-6s %-9s | %-9s %-11s %-9s %s\n",
        "preset", "base", "top", "cover", "feature", "type", "meanDens", "coverage", "components",
        "largest", "edgeDensity");
    for (atmosphere::CloudPreset preset : {atmosphere::CloudPreset::Cumulus,
            atmosphere::CloudPreset::Stratus, atmosphere::CloudPreset::Cirrus}) {
        atmosphere::AtmosphereParameters p;
        p.enabled = true;
        p.sunElevationDegrees = 45.0f;
        p.sunAzimuthDegrees = 136.0f;
        p.skyIntensity = 3.0f;
        p.sunIntensity = 3.0f;
        p.groundAlbedo = 0.25f;
        atmosphere::applyCloudPreset(p, preset);

        cloud::MarchSettings settings;
        const atmosphere::CloudTierBudget budget =
            atmosphere::cloudTierBudget(atmosphere::CloudQualityTier::Low);
        settings.primarySteps = budget.primarySteps;
        settings.lightSteps = budget.lightSteps;
        settings.extinction = cloud::volumetricExtinction;
        settings.sunDirection = atmosphere::sunDirection(p);

        const ShapeMetrics metrics = measureShape(p, settings, 96, 64);
        const char* name = preset == atmosphere::CloudPreset::Cumulus ? "cumulus"
            : (preset == atmosphere::CloudPreset::Stratus ? "stratus" : "cirrus");
        std::printf("%-8s %-7.0f %-7.0f %-6.2f %-8.0f %-6.2f %-9.3f | %-9.3f %-11d %-9.3f %.3f\n",
            name, p.cloudBaseHeight, p.cloudTopHeight, p.cloudCoverage, p.cloudFeatureScale,
            p.cloudType, metrics.meanDensity, metrics.coverage, metrics.components,
            metrics.largest, metrics.edgeDensity);
    }
}

// The weather map as a hashable offline asset (step C5's "CPU generated" half, and the first
// instance of the pattern step C7 needs).
//
// The field is evaluated procedurally on both sides rather than sampled from a texture -- see
// `myrenderer_cloud_weather` for why. What that leaves missing is the ability to *look* at the map
// and to pin its identity, so this dumps exactly the field the shader evaluates, one texel per
// sample, and prints an FNV-1a hash of the bytes.
//
// The hash is the useful half. A rendered frame that used a different weather field cannot be
// compared against a baseline, and "the seed did not change" is not something a diff of two PNGs can
// establish. This is: a byte-identical dump means the field is byte-identical, on any machine, with
// no floating point in the comparison itself.
//
// Format is binary PPM (P6), which every image tool reads and which needs no library. The map
// repeats, so the dump is written at 256x256 covering exactly one period: `u` runs over one whole
// wrap in both axes, which is also what makes the seam visible in the dump.
void dumpWeatherMap(const std::filesystem::path& path, int size) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size) * size * 3U, 0U);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);
            for (int channel = 0; channel < 3; ++channel) {
                const float value = atmosphere::cloudWeather(u, v, channel);
                const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
                pixels[(static_cast<std::size_t>(y) * size + x) * 3U
                    + static_cast<std::size_t>(channel)] =
                        static_cast<unsigned char>(clamped * 255.0f + 0.5f);
            }
        }
    }
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : pixels) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::printf("weather map: could not open %s\n", path.string().c_str());
        return;
    }
    file << "P6\n" << size << " " << size << "\n255\n";
    file.write(reinterpret_cast<const char*>(pixels.data()),
        static_cast<std::streamsize>(pixels.size()));
    std::printf("weather map: %s (%dx%d, R=coverage G=type B=height)\n", path.string().c_str(),
        size, size);
    std::printf("weather map: FNV-1a %016llx\n", static_cast<unsigned long long>(hash));
}

// A bounded, fixed-camera measurement for the current volume field. Unlike the historical sweeps,
// this compares the tiers per pixel, and writes the measured transmittance for visual inspection.
int acceptance(const std::filesystem::path& directory) {
    constexpr int width = 96;
    constexpr int height = 64;
    std::filesystem::create_directories(directory);
    std::ofstream report(directory / "shape.json");
    if (!report) return 2;
    report << "{\n  \"width\": 96, \"height\": 64, \"densitySamples\": 64,\n  \"presets\": [\n";
    bool passed = true;
    int presetIndex = 0;
    for (auto preset : {atmosphere::CloudPreset::Cumulus,
            atmosphere::CloudPreset::Stratus, atmosphere::CloudPreset::Cirrus}) {
        atmosphere::AtmosphereParameters p;
        p.enabled = true;
        p.sunElevationDegrees = 45.0f;
        atmosphere::applyCloudPreset(p, preset);
        const char* name = preset == atmosphere::CloudPreset::Cumulus ? "cumulus"
            : (preset == atmosphere::CloudPreset::Stratus ? "stratus" : "cirrus");
        std::array<std::vector<float>, 2> images;
        std::array<ShapeMetrics, 2> metrics;
        for (int tier = 0; tier < 2; ++tier) {
            cloud::MarchSettings settings;
            const auto budget = atmosphere::cloudTierBudget(tier == 0
                ? atmosphere::CloudQualityTier::Low : atmosphere::CloudQualityTier::High);
            settings.primarySteps = budget.primarySteps;
            settings.lightSteps = budget.lightSteps;
            settings.sunDirection = atmosphere::sunDirection(p);
            metrics[tier] = measureShape(p, settings, width, height, &images[tier]);
            std::ofstream ppm(directory / (std::string(name) + (tier == 0 ? "-low.ppm" : "-high.ppm")),
                std::ios::binary);
            ppm << "P6\n" << width << ' ' << height << "\n255\n";
            for (float value : images[tier]) {
                passed = passed && std::isfinite(value) && value >= 0.0f && value <= 1.0f;
                const unsigned char byte = static_cast<unsigned char>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
                const unsigned char pixel[3] = {byte, byte, byte};
                ppm.write(reinterpret_cast<const char*>(pixel), 3);
            }
            if (!ppm) return 2;
            std::printf("%s %s: meanDensity=%.6f coverage=%.6f components=%d largest=%.6f edgeDensity=%.6f\n",
                name, tier == 0 ? "low" : "high", metrics[tier].meanDensity, metrics[tier].coverage,
                metrics[tier].components, metrics[tier].largest, metrics[tier].edgeDensity);
        }
        double error = 0.0;
        std::size_t intersection = 0, unionCount = 0;
        for (std::size_t index = 0; index < images[0].size(); ++index) {
            error += std::abs(images[0][index] - images[1][index]);
            const bool low = images[0][index] < 0.5f, high = images[1][index] < 0.5f;
            intersection += low && high;
            unionCount += low || high;
        }
        error /= static_cast<double>(images[0].size());
        const double overlap = unionCount == 0 ? 1.0 : static_cast<double>(intersection) / unionCount;
        std::printf("%s tier comparison: transmittanceMAE=%.6f silhouetteIoU=%.6f\n", name, error, overlap);
        // Same field at two quadrature budgets must keep its silhouette and transmission. Empty
        // cirrus silhouettes are allowed: this diagnostic's 50% opacity contour cannot see wisps.
        // IoU is ill-conditioned for almost transparent presets (a handful of contour pixels).
        // Bound the absolute contour disagreement as well; this never excuses a large mismatch.
        const double disagreement = static_cast<double>(unionCount - intersection) / images[0].size();
        passed = passed && error < 0.03 && (overlap > 0.90 || disagreement < 0.002);
        for (const auto& m : metrics) {
            if (preset == atmosphere::CloudPreset::Cumulus) {
                passed = passed && m.coverage > 0.20 && m.coverage < 0.70
                    && m.components >= 2 && m.largest < 0.90
                    && m.edgeDensity > 0.10 && m.edgeDensity < 0.30;
            } else if (preset == atmosphere::CloudPreset::Stratus) {
                passed = passed && m.coverage > 0.70 && m.edgeDensity < 0.15;
            } else {
                passed = passed && m.coverage < 0.10 && m.meanDensity > 0.0;
            }
        }
        if (presetIndex > 0) report << ",\n";
        report << "    {\"name\": \"" << name << "\", \"transmittanceMAE\": " << error
            << ", \"silhouetteIoU\": " << overlap << ", \"silhouetteDisagreement\": " << disagreement << ", \"tiers\": [";
        for (int tier = 0; tier < 2; ++tier) {
            const auto& m = metrics[tier];
            if (tier != 0) report << ',';
            report << "{\"name\": \"" << (tier == 0 ? "low" : "high")
                << "\", \"meanDensity\": " << m.meanDensity << ", \"coverage\": " << m.coverage
                << ", \"components\": " << m.components << ", \"largest\": " << m.largest
                << ", \"edgeDensity\": " << m.edgeDensity << '}';
        }
        report << "]}";
        ++presetIndex;
    }
    report << "\n  ], \"passed\": " << (passed ? "true" : "false") << "\n}\n";
    if (!report) return 2;
    std::printf("Cloud shape acceptance: %s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--hero") {
        SceneDocument scene;
        std::string error;
        if (!loadSceneDocument(argv[2], scene, error)) {
            std::fprintf(stderr, "Hero input failed: %s\n", error.c_str());
            return 2;
        }
        const std::filesystem::path output(argv[3]);
        std::filesystem::create_directories(output);
        Camera camera;
        camera.setOrbitPose(scene.camera.target, scene.camera.yawDegrees, scene.camera.pitchDegrees,
            scene.camera.distance, scene.camera.fieldOfViewDegrees);
        std::array<std::vector<float>, 2> images;
        std::array<ShapeMetrics, 2> metrics;
        for (int tier = 0; tier < 2; ++tier) {
            cloud::MarchSettings settings;
            const auto budget = atmosphere::cloudTierBudget(tier == 0
                ? atmosphere::CloudQualityTier::Low : atmosphere::CloudQualityTier::High);
            settings.primarySteps = budget.primarySteps;
            settings.lightSteps = budget.lightSteps;
            settings.sunDirection = atmosphere::sunDirection(scene.renderer.atmosphere);
            metrics[tier] = measureShape(scene.renderer.atmosphere, settings, 96, 54, &images[tier], &camera);
            std::ofstream ppm(output / (tier == 0 ? "low.ppm" : "high.ppm"), std::ios::binary);
            ppm << "P6\n96 54\n255\n";
            for (float value : images[tier]) {
                if (!std::isfinite(value) || value < 0 || value > 1) return 1;
                const unsigned char byte = static_cast<unsigned char>(value * 255.0f + 0.5f);
                const unsigned char pixel[3] = {byte, byte, byte};
                ppm.write(reinterpret_cast<const char*>(pixel), 3);
            }
            if (!ppm) return 2;
        }
        double mae = 0;
        int intersection = 0, unionCount = 0;
        for (std::size_t i = 0; i < images[0].size(); ++i) {
            mae += std::abs(images[0][i] - images[1][i]);
            const bool low = images[0][i] < 0.5f, high = images[1][i] < 0.5f;
            intersection += low && high;
            unionCount += low || high;
        }
        mae /= images[0].size();
        const double iou = unionCount > 0 ? static_cast<double>(intersection) / unionCount : 0;
        const bool passed = mae < 0.03 && iou > 0.90 && metrics[1].coverage > 0.05
            && metrics[1].coverage < 0.55 && metrics[1].components >= 2;
        std::ofstream report(output / "shape.json");
        report << "{\"width\":96,\"height\":54,\"mae\":" << mae << ",\"iou\":" << iou
            << ",\"lowCoverage\":" << metrics[0].coverage << ",\"highCoverage\":" << metrics[1].coverage
            << ",\"components\":" << metrics[1].components << ",\"passed\":" << (passed ? "true" : "false") << "}\n";
        if (!report) return 2;
        std::printf("Hero Low/High transmission MAE %.6f, IoU %.6f, coverage %.6f, components %d: %s\n",
            mae, iou, metrics[1].coverage, metrics[1].components, passed ? "PASS" : "FAIL");
        return passed ? 0 : 1;
    }
    if (argc == 2 && std::string(argv[1]) == "--preset-sweep") {
        for (auto preset : {atmosphere::CloudPreset::Cumulus, atmosphere::CloudPreset::Stratus}) {
            for (float coverage : {0.50f, 0.55f, 0.60f, 0.70f, 0.85f, 0.95f}) {
                atmosphere::AtmosphereParameters p;
                p.enabled = true;
                atmosphere::applyCloudPreset(p, preset);
                p.cloudCoverage = coverage;
                cloud::MarchSettings settings;
                settings.primarySteps = 24;
                settings.lightSteps = 4;
                const auto m = measureShape(p, settings, 96, 64);
                std::printf("%s coverage=%.2f: meanDensity=%.6f coverage=%.6f components=%d largest=%.6f edgeDensity=%.6f\n",
                    preset == atmosphere::CloudPreset::Cumulus ? "cumulus" : "stratus", coverage,
                    m.meanDensity, m.coverage, m.components, m.largest, m.edgeDensity);
            }
        }
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--acceptance") {
        if (argc != 3) {
            std::fprintf(stderr, "Usage: MyRendererCloudCalibration --acceptance OUTPUT_DIRECTORY\n");
            return 2;
        }
        return acceptance(argv[2]);
    }
    // The dump path is a command-line argument rather than a constant so a build never writes into
    // the source tree by default: `MyRendererCloudCalibration` with no arguments prints tables only.
    if (argc > 1) dumpWeatherMap(argv[1], 256);
    // The C1 tables below still run: they measure the *analytic layer*, which is what the CPU path
    // tracer's environment capture reads, and C5 changed the density field under both of them.
    std::printf("--- cloud base height sweep (feature 6400, coverage 0.45) ---\n");
    for (float base : {1600.0f, 2400.0f, 3200.0f, 4800.0f, 6400.0f}) {
        evaluate({base, base * 2.5f, 6400.0f, 0.45f, 1.0f});
    }
    std::printf("--- feature scale sweep (base 3200, top 8000, coverage 0.45) ---\n");
    for (float feature : {3200.0f, 4800.0f, 6400.0f, 9600.0f, 12800.0f}) {
        evaluate({3200.0f, 8000.0f, feature, 0.45f, 1.0f});
    }
    std::printf("--- coverage sweep (base 3200, top 8000, feature 6400) ---\n");
    for (float coverage : {0.20f, 0.35f, 0.50f, 0.65f, 0.80f}) {
        evaluate({3200.0f, 8000.0f, 6400.0f, coverage, 1.0f});
    }
    // The shipped ambient defaults are this sweep's `ambientElevation=15, scale=0.85` row: it is the
    // only setting measured to make a cloud darker than the sky near the horizon and brighter above
    // it, which is the transition that lets a layer read as cloud instead of as flat haze.
    sweepAmbient(3200.0f, 8000.0f, 6400.0f, 0.50f);
    sweepMarch(3200.0f, 8000.0f, 6400.0f, 0.50f);
    // The C5 tables: the shape metric that judged the rework, and the presets chosen from it.
    sweepShape(3200.0f, 8000.0f);
    sweepDetail(3200.0f, 8000.0f, 0.50f);
    sweepWeather(3200.0f, 8000.0f);
    sweepTier(3200.0f, 8000.0f);
    evaluatePresets();
    return 0;
}
