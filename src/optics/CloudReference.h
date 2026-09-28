#pragma once

#include <glm/vec3.hpp>

#include "optics/Atmosphere.h"

// CPU reference raymarcher for the cloud layer.
//
// This is the project's golden reference for the volumetric step: it marches the same density
// field, with the same phase function and the same compositing rule, as `shaders/cloud_layer.frag`.
// Both sides include `CloudField.h` verbatim, so the only thing a GPU/CPU comparison can disagree
// about is the march itself -- step count, jitter sequence and accumulation order -- and not the
// model. That is the property the acceptance test is built to check, and it is the reason this
// exists from C2 rather than at the end.
//
// Each sample marches towards the sun and composites front to back. C3 adds a bounded multi-octave
// phase approximation; this is not a full multiple-scattering transport solver.
namespace cloud {
// Sun-ray transmission through the full cloud slab. Receivers must be below the cloud base.
float shadowTransmittance(const glm::vec3& receiver,
    const atmosphere::AtmosphereParameters& parameters, int steps, float extinction);

// The volume march's calibrated constants (P1-A slice 6, step C2).
//
// These are *not* the analytic layer's constants, and the difference matters. C1's coefficients were
// chosen for a single density sample per direction; a march accumulates dozens, each weighted by
// `1 - exp(-density * step * extinction)`, so the same lighting produces a far brighter result. The
// first frame with the march switched on blew the sky out to white, which is what sent these numbers
// to `MyRendererCloudCalibration` rather than to more image editing.
//
// Rechecked after the 3D density repair with `--acceptance` at 96x64, elevation 15..55 degrees.
// At extinction 0.0025 the calibrated cumulus preset covers 30.7% of the view, with edgeDensity
// 0.160 and Low/High transmittance MAE 0.00408. Stratus covers 88.7%. These measurements pin shape
// and tier consistency; they do not establish photorealistic lighting or a physical unit scale.
inline constexpr float volumetricExtinction = 0.0025f;
// The ambient term is the sky above the layer. The sun term carries the phase function, whose
// forward peak is about 23, so it is scaled far below the ambient term or the silver lining becomes
// a hole burned through the layer instead of a bright rim.
inline constexpr float volumetricAmbientScale = 1.0f;
inline constexpr float volumetricSunScale = 0.05f;

// The lighting a scene's atmosphere parameters imply for the march, in one place so the renderer and
// any diagnostic agree. Both values already include the calibrated scales above: the march applies
// the phase function and the light march to `sun` itself, so a caller that scaled it again would be
// tuning the same number twice.
struct MarchLighting {
    glm::vec3 ambient{0.0f};
    glm::vec3 sun{0.0f};
};

// Derives that lighting from the analytic sky, which is what makes the layer sit in the same
// atmosphere as everything else rather than in a lighting rig of its own.
MarchLighting marchLighting(const atmosphere::AtmosphereParameters& parameters);

// Horizontal reach of the view march. A planar slab has an infinite grazing-ray span; tying the
// cap to the weather and shape scales keeps that singularity from undersampling a repeating field.
float maximumViewDistance(const atmosphere::AtmosphereParameters& parameters);

// The slab a view ray crosses, in world units along the ray. `valid` is false when the ray misses
// the slab entirely, which includes every downward ray from above the layer.
struct SlabSpan {
    bool valid{false};
    float start{0.0f};
    float length{0.0f};
};

// Intersects a ray with the layer's slab. `originY` and `directionY` are the camera height and the
// ray's vertical component, in the same world units as the layer heights.
SlabSpan slabSpan(
    float originY,
    float directionY,
    float baseHeight,
    float topHeight
);

struct MarchSettings {
    // Steps along the view ray. The brief's range is 24..48 for the low tier; the reference uses
    // whatever the caller asks for so a GPU tier can be compared against the same budget.
    int primarySteps{32};
    // Steps towards the sun per valid sample. The brief measures the useful range at 4..6.
    int lightSteps{6};
    // Extinction per unit density per world unit; see the calibration note above.
    float extinction{volumetricExtinction};
    // Offset of the whole march along the ray, in *world units*, on top of centred sampling. World
    // units and not a fraction of a step on purpose: a fraction would move the samples when the step
    // count changes, so the integral would depend on the budget and there would be no value to
    // converge to. Centred sampling plus a zero offset is the reference.
    float jitter{0.0f};
    // Raster pixels use a deterministic interval offset to turn coherent marching bands into fine
    // grain. The CPU integral keeps centred samples; parity comparisons explicitly leave this off.
    bool spatialJitter{false};
    // Ambient sky radiance lighting the layer. The caller supplies it from the analytic sky, scaled,
    // so the cloud is lit by the same atmosphere it sits in.
    glm::vec3 ambientRadiance{0.0f};
    // Sun direction (unit, pointing from the scene towards the sun) and the sun's radiance.
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f};
    glm::vec3 sunRadiance{0.0f};
    // The multiple-scattering approximation (C3). `octaves` is how many extra phase evaluations each
    // sample pays for, `attenuation` how fast their energy falls off, and `eccentricity` how quickly
    // each successive octave forgets the sun's direction. See `myrenderer_cloud_multi_phase` for why
    // the weights sum to one and what that is protecting.
    //
    // The defaults are the brief's middle of the measured ranges: 4..6 octaves, `a` in 0.5..0.7. The
    // count is deliberately at the low end -- each octave is one more phase evaluation per covered
    // sample, and the march already pays for the primary and light marches.
    int multiScatterOctaves{4};
    float multiScatterAttenuation{0.6f};
    float multiScatterEccentricity{0.6f};
    // Whether the powdered-edge term is applied. It darkens a cloud's thin edges, which is what stops
    // a silhouette from glowing.
    bool powder{true};
};

struct MarchResult {
    // Radiance scattered towards the camera by the layer, already premultiplied by its coverage.
    glm::vec3 radiance{0.0f};
    // Fraction of the background that reaches the camera through the layer: 1 is clear.
    float transmittance{1.0f};
};

// The layer's normalised density at a world position: the one function the analytic sky, this
// reference marcher and the GPU shader all evaluate. Exposed because it is the natural unit of
// testing -- a march result is an integral, but the field it integrates has its own contracts
// (tiling, coverage monotonicity, translation by wind) that are worth asserting directly.
float densityAt(const glm::vec3& position, const atmosphere::AtmosphereParameters& parameters);

// The multiple-scattering phase function, exposed for the same reason `densityAt` is: it is the
// natural unit of testing for the term C3 adds. Asserting on a march result instead would only show
// that *something* changed, not that the phase function is still normalised -- and an unnormalised
// one silently rescales the cloud's brightness under the guise of adding a fill.
float multiScatterPhase(float cosViewSun, int octaves, float attenuation, float eccentricity);

// The powdered-edge term, likewise. It darkens a cloud's thin edges; without it a silhouette glows.
float powderFactor(float density, float distance);

// Marches one view ray. `origin` is the camera position in world units. Deterministic in its
// arguments alone.
MarchResult march(
    const glm::vec3& origin,
    const glm::vec3& direction,
    const atmosphere::AtmosphereParameters& parameters,
    const MarchSettings& settings
);

// Number of density evaluations `march` performs for this ray: the primary march plus the light
// march of every sample that landed inside the cloud. Reported so a test can assert the cost model
// rather than infer it, and so the editor can state it.
int densitySampleCount(
    const glm::vec3& origin,
    const glm::vec3& direction,
    const atmosphere::AtmosphereParameters& parameters,
    const MarchSettings& settings
);

} // namespace cloud
