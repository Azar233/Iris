#pragma once

#include <vector>

#include <glm/vec3.hpp>

// Analytic single-scattering sky shared by every consumer of the environment: the raster
// skybox cubemap, its irradiance / prefiltered mips, the CPU path tracer's environment
// sampling and the headless batch runtime all evaluate this one model, so a rendered sky
// and a traced sky cannot disagree.
//
// Model and its limits (deliberate, documented rather than implied):
//   - Rayleigh and Mie single scattering with a closed-form exponential-atmosphere integral,
//     `beta * H * m * (1 - exp(-tau)) / tau`, where `m` is the Kasten-Young relative air mass
//     and `tau` the air mass scaled vertical optical depth. No numerical march, so the
//     evaluator stays cheap enough to rebuild environment cubemaps.
//   - The sun's transmittance is evaluated at the ground and treated as constant along the
//     view ray. That is the standard practical approximation and keeps the sky blue overhead
//     and red at sunset.
//   - No multiple scattering and no ozone layer: the zenith is therefore darker than reality
//     during twilight and the deep-blue twilight band is missing. Both are recorded as
//     follow-ups in `docs/atmosphere-sky.md` rather than faked.
//   - Directions below the horizon return the radiance a Lambertian ground of `groundAlbedo`
//     reflects: the cosine-weighted sky irradiance plus the direct sun, so the environment's
//     lower hemisphere carries the same sun the key light does. They never return NaN.

namespace atmosphere {

// Quality tiers, which for the layer means how much of the integral is actually paid for. The
// brief's measured ranges are 24..48 primary steps and 4..6 light steps; these are the ends of them.
//
// The tier deliberately does *not* change any density-field parameter. A tier that also edited the
// noise would make the Low and High images differ for two reasons at once, and the C5 acceptance
// contract -- the same weather map gives the same structure at both tiers -- would be untestable.
//
// It lives on the parameters rather than in `Renderer` because the march budget changes the pixels,
// which makes it part of what a cached environment and a saved scene have to agree on. It is
// declared before `AtmosphereParameters` for the boring reason that the struct holds one.
enum class CloudQualityTier {
    Low,
    High,
};

struct AtmosphereParameters {
    bool enabled{false};
    // Sun position in degrees: elevation above the horizon, azimuth measured from +Z towards +X.
    float sunElevationDegrees{35.0f};
    float sunAzimuthDegrees{135.0f};
    // Mie (aerosol) multiplier on the sea-level coefficient 21e-6 /m. 1 is a clean,
    // ~20-30 km visibility sky; larger values are hazier and grey out the zenith.
    float turbidity{1.0f};
    float skyIntensity{1.0f};
    float sunIntensity{1.0f};
    float groundAlbedo{0.10f};
    // Optional artistic night sky. Off for existing scenes; the moon follows a repeatable
    // presentation orbit driven by the sun, not a calendar-based ephemeris.
    bool nightSkyEnabled{false};
    float moonIntensity{1.0f};
    float starIntensity{1.0f};
    // Aerial perspective: the air between the camera and the geometry, integrated with the same
    // coefficients as the sky (`opticalDepthAlongSegment`). Off by default so a scene that never
    // asked for it keeps exactly the pixels it had.
    bool aerialPerspectiveEnabled{false};
    float aerialPerspectiveStrength{1.0f};
    // Scale height of the density profile, expressed in *world* units so a scene can tune the
    // effect without knowing the model's metre convention: the total atmospheric column is
    // `scaleHeight * worldsPerMetre` metres. Distances beyond a few scale heights stop fading.
    float aerialPerspectiveScaleHeight{60.0f};

    // Cloud layer heights in the same *world* units as `aerialPerspectiveScaleHeight`, so a scene
    // that measures in metres gets physical altitudes. `topHeight <= baseHeight` collapses the slab
    // to a plane and removes the thickness term without producing a degenerate direction.
    //
    // Off by default: a scene that never asked for clouds keeps exactly the pixels it had.
    bool cloudsEnabled{false};
    float cloudBaseHeight{3200.0f};
    float cloudTopHeight{8000.0f};
    // Fraction of the sky the layer covers, in 0..1: 0 is clear, 1 is overcast.
    //
    // Step C1 shipped this as a coverage *threshold*, so larger meant less cloud while the weather
    // map's red channel means more. Two coverage knobs running in opposite directions is how a
    // parameter set ends up silently inverted, so C5 flipped it: this is now the fraction covered,
    // matching `cloudCoverageVariation` and the map. A scene file written before the flip reads its
    // old value as a coverage, which for the shipped default (0.45) is within a few per cent of the
    // C1 frame it produced -- see docs/cloud-layer-c1.md.
    float cloudCoverage{0.50f};
    // Multiplies the resulting density, i.e. how dark and solid the covered part reads.
    float cloudDensity{1.0f};
    // Horizontal offset of the noise field, in *world* units like the heights above. Carrying the
    // layer's own advection in this one value keeps the module that drives the animation free of
    // cloud-specific parameters and keeps the same offset reproducible for a given frame.
    float cloudWindOffsetX{0.0f};
    float cloudWindOffsetZ{0.0f};
    // World-unit size of one shape feature: the noise repeats every
    // `cloudFeatureScale * cloudNoisePeriod` world units. Size this against the scene's own unit
    // scale -- a scene whose world unit is 133 metres gets ~150 km features at the default, which is
    // the scale of a real cloud street.
    float cloudFeatureScale{6400.0f};
    float cloudNoisePeriod{4.0f};

    // ---- The 2D weather map (P1-A slice 6, step C5) ------------------------------------------
    //
    // The layer is no longer uniform across the sky: a second, much coarser noise field varies its
    // coverage, its cloud type and its altitude from one air mass to the next. That field is the
    // weather map, and these are its controls. They are what turns "a density threshold" into
    // "clouds here, clear sky there".
    //
    // World-unit size of one repeat of the weather pattern. Deliberately four times the feature
    // scale at the default: the weather describes masses of cloud, not individual clouds.
    float cloudWeatherScale{25600.0f};
    // How far the map's red channel may swing the local coverage away from `cloudCoverage`. 0 makes
    // the map inert, which is the control case the structure test compares against. At 1 the local
    // coverage sweeps the whole 0..1 range around the layer mean, so a scene can go from broken
    // cloud to clear sky within one frame.
    float cloudCoverageVariation{0.90f};
    // The map's green channel: 0 is a flat layered deck (stratus), 1 a bottom-heavy convective
    // cloud (cumulus). The preset sets the layer-wide value; this channel moves regions away from it.
    float cloudType{0.65f};
    float cloudTypeVariation{0.50f};
    // The map's blue channel: how far the vertical profile may slide up and down inside the slab, as
    // a fraction of its thickness. This is what gives the layer a bumpy base without a per-sample
    // slab, which the march's span, the sun march and the analytic layer all depend on being fixed.
    float cloudHeightVariation{0.30f};
    // How strongly the two finest octaves carve the *interior* of a cloud, and how much of that
    // carving survives at its silhouette. The first is what gives a cloud volume; the second is the
    // measured difference between a smooth outline and the filigree that C2 produced. See
    // `myrenderer_cloud_density` and the shape sweep in docs/cloud-layer-c1.md.
    float cloudDetailStrength{0.45f};
    float cloudDetailEdge{0.15f};
    // Missing fields in old scenes retain the original cloud transport.
    bool cloudHeightLighting{false};
    float cloudShapeBlend{0.0f};
    // Quality changes quadrature budgets, independently of raster resolution and temporal history.
    CloudQualityTier cloudQuality{CloudQualityTier::Low};
    // Raster-only optimization; old scenes keep their full-resolution deterministic output.
    bool cloudHalfResolution{false};
    bool cloudTemporalEnabled{false};
    bool cloudShadowsEnabled{false};
    bool cloudGodRaysEnabled{false};
    bool cloudDeterministic{false};
    // Fixed 64 cubed period-4 input, shared by the raster passes and CPU reference.
    bool cloudOfflineNoise{false};
    float cloudGodRaysStrength{0.08f};
    // Width in degrees of the fade that hides the projection's singularity at the horizon.
    float cloudHorizonFadeDegrees{6.0f};
    // Elevation of the sky direction a cloud is lit by, and how much of it reaches the cloud. These
    // are lighting parameters rather than shape ones: a cloud sits in the dense bright air below it,
    // so the sky that lights it is far brighter than the zenith, and this is what decides whether a
    // cloud reads as brighter or darker than the sky it covers.
    float cloudAmbientElevationDegrees{15.0f};
    float cloudAmbientScale{0.85f};

    // The volume march's light scales. These are *not* the two above: `cloudAmbientScale` multiplies
    // the analytic layer's single sample, while these multiply what an integrating march accumulates,
    // and a march sums dozens of weighted samples. Keeping them separate is what lets the two models
    // be calibrated independently instead of one breaking when the other is tuned. The defaults and
    // their measured justification are recorded in `cloud::volumetricAmbientScale`.
    float cloudVolumetricAmbientScale{4.0f};
    float cloudVolumetricSunScale{0.05f};
};

// ---- Cloud type presets (P1-A slice 6, step C5) ----------------------------------------------
//
// A preset is a *starting point* for the layer parameters, not a runtime mode: `applyCloudPreset`
// writes into an `AtmosphereParameters` and nothing downstream remembers which preset it came from.
// That keeps the saved scene the single source of truth -- a scene that came from a preset and was
// then edited reopens as what it is, and the preset only re-enters if a user asks for it again.
enum class CloudPreset {
    // Convective, bottom-heavy, a few kilometres thick. The default look.
    Cumulus,
    // A broad flat deck: shallow, near-uniform vertically, low contrast between its base and top.
    Stratus,
    // A high, thin, wispy sheet. See the note on `applyCloudPreset` for what the slab model does and
    // does not reproduce here.
    Cirrus,
};

struct CloudTierBudget {
    int primarySteps{32};
    int lightSteps{6};
};

CloudTierBudget cloudTierBudget(CloudQualityTier tier);

// Writes the preset's shape, altitude and density-field parameters into `parameters`. Only cloud
// members are touched; the sun, the sky and the tier are the caller's business.
//
// The tier is deliberately not a parameter here. It changes how much of the integral is paid for,
// not what the field is, and folding the two together would make the C5 acceptance contract -- the
// same weather map gives the same structure at both tiers -- impossible to state.
//
// Known limitation, recorded rather than papered over: the model has one slab and one vertical
// profile, so `Cirrus` can only be a thin high deck. Real cirrus is a hooked fallstreak -- dense at
// the top, trailing downwards -- which a single symmetric bump cannot express, and the preset
// therefore reads as a thin stratus at altitude. A separate density profile for the thin-high case
// is a follow-up, not something this preset fakes.
void applyCloudPreset(AtmosphereParameters& parameters, CloudPreset preset);

// Unit vector pointing from the scene towards the sun. The raster directional light travels
// along `-sunDirection()`, which is what `RendererSettings::lightDirection` stores.
glm::vec3 sunDirection(const AtmosphereParameters& parameters);
glm::vec3 moonDirection(const AtmosphereParameters& parameters);
float nightVisibility(const AtmosphereParameters& parameters);
float moonKeyStrength(const AtmosphereParameters& parameters);

// Whether two parameter sets are the same *in a render*. Evaluating the sky is expensive -- an
// environment rebuild is a few hundred milliseconds, an equirectangular radiance map is tens -- so
// callers cache the result and only pay for it when the sun moved far enough to be visible or a
// parameter moved by more than float noise. Keeping the tolerances here means the raster
// environment, the CPU path tracer's sky and any future consumer agree on what "changed" means
// instead of each inventing its own threshold.
bool parametersMatch(const AtmosphereParameters& a, const AtmosphereParameters& b);

// Cloudless raster sky/IBL inputs only. Camera position, clouds and aerial perspective
// are evaluated by separate frame passes and must never trigger this expensive bake.
bool environmentParametersMatch(const AtmosphereParameters& a, const AtmosphereParameters& b);

// Spectral radiance of the sky in `viewDirection` (unit, +Y up). Finite and non-negative for
// every input, including a sun below the horizon.
glm::vec3 skyRadiance(const glm::vec3& viewDirection, const AtmosphereParameters& parameters);

// Colour of the direct sun after atmospheric extinction, before `sunIntensity` is applied.
// Intended as the directional light colour so the light and the sky stay consistent.
glm::vec3 sunTransmittance(const AtmosphereParameters& parameters);

// Optical depth of the whole vertical column, per RGB channel: what a ray from the ground straight
// up accumulates. This is the scale aerial perspective is expressed in -- a ray walking
// `length / scaleHeight` scale heights horizontally accumulates one whole column -- and it is
// `-log(sunTransmittance())` for a sun at the zenith.
glm::vec3 verticalOpticalDepth(const AtmosphereParameters& parameters);

// `sunTransmittance` normalised so its brightest channel is 1: the *spectrum* of the key light
// with no brightness left in it. Brightness and colour are separate concerns because the raster
// key light scales the existing `diffuseStrength` / `specularStrength` by the transmittance
// luminance; multiplying by the raw transmittance as well would apply the extinction twice. A
// disabled (or fully extinguished) atmosphere returns white, so a scene without a sky keeps the
// neutral light it has always had.
glm::vec3 skyLightColor(const AtmosphereParameters& parameters);

// Radiance of the sun disk for the given view direction, already scaled by intensity. Kept
// separate from `skyRadiance` so callers that only want the atmosphere can skip the disk.
glm::vec3 sunDiskRadiance(const glm::vec3& viewDirection, const AtmosphereParameters& parameters);

// The cloud layer above the camera, evaluated as the sky's single source of cloud truth.
//
// Scope, stated rather than implied -- this is P1-A slice 6 step C1 and it is deliberately not a
// volumetric model yet:
//   - The layer is a horizontal slab between `cloudBaseHeight` and `cloudTopHeight`, but the shape
//     is sampled *once* at the slab's mid-plane and given the analytic geometric path length
//     through the slab as its thickness. There is no ray march, so there is no self-shadowing and
//     no internal structure: a cloud is flat, and only its silhouette varies with the view angle.
//   - `cloudLayer` therefore returns one opaqueness and one colour for the whole column along the
//     view ray. `cloudAmbientRadiance` is what that column scatters toward the camera, and it is
//     multiplied by `1 - mask` when compositing, so the layer attenuates rather than replaces.
//   - Directions at or below the horizon return zero. The error this hides grows as the slab is
//     approached from above and is zero when the camera is at or above `cloudTopHeight`; the
//     renderer exposes `CloudLayer::minimumCameraHeight` for the diagnostic readout.
struct CloudLayer {
    // Fraction of the ray the layer covers: 0 leaves the sky untouched, 1 occludes it.
    float mask{0.0f};
    // Scattered radiance of that column, already scaled by intensity. Linear, like the sky.
    glm::vec3 ambient{0.0f};
    // Geometric path length through the slab along this ray, in world units: the closed form the
    // layer's thickness term is built from, reported so a CPU check can compare against it without
    // re-deriving the projection. A vertical ray gives exactly `topHeight - baseHeight`.
    float pathLength{0.0f};
    // The lowest camera height at which this view direction has no projection error. Not used by
    // the layer itself; reported so the editor can state the approximation instead of hiding it.
    float minimumCameraHeight{0.0f};
};

// The cloud layer seen from `cameraHeight` (world units above ground) along `viewDirection`.
// Deterministic in its arguments alone, so the raster sky, the CPU path tracer's environment and
// any headless render agree by construction. Returns zero for a disabled layer, a non-finite
// direction or a direction at or below the horizon.
CloudLayer cloudLayer(
    const glm::vec3& viewDirection,
    const AtmosphereParameters& parameters,
    float cameraHeight
);

// Double-lobed Henyey-Greenstein phase function for the cloud layer, evaluated at the cosine
// between the view ray and the sun. Forward scattering dominates (the silver lining), with a
// weaker backward lobe. Exposed so a CPU reference integrator can use the same function the
// analytic layer uses instead of re-deriving it.
float cloudPhase(float cosViewSun);

// The cloud layer's *base* density field, in tile coordinates: `tileX` / `tileY` are world-plane
// positions divided by the layer's feature scale. This is the two-octave field that decides where a
// cloud is; the finest octaves that carve its interior are `cloudDetailShape` and deliberately do not
// take part in the silhouette. Deterministic integer hashing only -- no table, no random state, no
// sampling-order dependence -- so the raster skybox, the CPU path tracer's captured environment and
// every headless render agree by construction.
//
// The field repeats exactly every `period` whole cells: shifting a sample by `period` in either
// axis, at a point inside a cell, reproduces the value bit for bit. That is the tiling contract the
// layer's unbounded horizontal projection depends on, and it is asserted directly by
// `atmosphere-model`. A point sitting exactly on a cell boundary is not covered by that contract:
// it may fall either side of the boundary after a projection's rounding and legitimately sample a
// different 2x2 neighbourhood. Turning a layer by an arbitrary angle therefore needs no care, but a
// *test* must not place its probe on a boundary and expect equality.
float cloudShape(float tileX, float tileY, int period);

// The interior-carving octaves: fine structure that is only ever subtracted from inside a cloud that
// the base shape already formed. Exposed for the same reason `worley2x2` is, so the contract "detail
// cannot change the silhouette" can be checked on the primitive rather than inferred from an image.
float cloudDetailShape(float tileX, float tileY, int period = 4);

// The 2D weather map: `channel` 0 is coverage, 1 is cloud type, 2 is height. `tileX` / `tileY` are
// world-plane positions divided by `cloudWeatherScale`. Tileable on the same terms as `cloudShape`,
// and decorrelated between channels, so raising the red channel's contrast does not also move the
// cloud type.
float cloudWeather(float tileX, float tileY, int channel);

// One octave of the field above: tileable Worley (cellular) noise, exposed so the tiling contract
// can be checked on the primitive rather than only through the fBm sum.
float worley2x2(float x, float y, int period);

// The sky as a rendered environment: `skyRadiance`, the cloud layer composited over it, and the
// sun disk attenuated by whatever cloud covers that direction. This is the single entry point the
// raster skybox, its prefiltered specular mips and the CPU path tracer's equirectangular capture
// all use, so a cloud seen in a reflection is the same cloud seen against the sky.
//
// The cloud layer deliberately does *not* enter the irradiance probe: the diffuse source stays
// `skyRadiance`. The layer is a bright attenuator -- adding its scattered term to the irradiance
// would add light the key light never produced -- and what the layer correctly removes from the
// sky is light-transport work belonging to C3, not a side effect of the C1 shape model.
glm::vec3 environmentRadiance(
    const glm::vec3& viewDirection,
    const AtmosphereParameters& parameters,
    float cameraHeight
);

// Angular radius of the rendered sun disk in degrees. Slightly wider than the real 0.265 deg
// so the disk is not lost at environment cubemap resolutions.
float sunAngularRadiusDegrees();

// Irradiance a surface facing the sun receives, in the same units as `skyRadiance`. The disk's
// radiance scale is chosen so the clear-sky ratio `E_sun / E_sky` is about 10, which is what
// makes the environment's ground hemisphere and its specular reflections see the same sun the
// analytic key light does.
glm::vec3 sunIrradiance(const AtmosphereParameters& parameters);

// Optical depth between two points, per RGB channel, for a ray of `segmentLength` world units whose
// direction has a vertical component of `directionY`. `worldUnitsPerMetre` converts the scene's
// world units into the model's metres, so the coefficients and the scale height keep their physical
// meaning while a scene stays free to use any unit scale.
//
// The vertical density profile is `rho(y) = exp(-(y - eyeY) / scaleHeight)`, so the column a ray
// accumulates is `airMass * (1 - exp(-heightDelta / scaleHeight))` times the constituent's own
// vertical depth, where the height difference along the segment is `segmentLength * directionY`.
// A horizontal ray therefore accumulates one scale height's worth of column per scale height
// travelled, a ray pointing up saturates on the full vertical column, and a segment of zero length
// is transparent. Finite and non-negative for every input, including a zero direction component.
glm::vec3 opticalDepthAlongSegment(
    const AtmosphereParameters& parameters,
    float segmentLength,
    float directionY,
    float worldUnitsPerMetre
);

// Deterministic equirectangular sampling of `environmentRadiance`: row 0 is the +Y pole and
// column 0 looks along +Z, matching the HDR loader used for file environments. `cameraHeight` is
// the viewpoint the cloud layer's parallax is resolved against, in the same world units as the
// layer heights; pass the eye height the render will use so a captured environment and a rendered
// skybox place the same cloud in the same direction.
std::vector<glm::vec3> generateEquirect(
    const AtmosphereParameters& parameters,
    int width,
    int height,
    float cameraHeight
);

} // namespace atmosphere
