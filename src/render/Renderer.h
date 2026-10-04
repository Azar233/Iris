#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "optics/Atmosphere.h"
#include "optics/PrismOptics.h"
#include "render/OpenGlStateCache.h"
#include "render/RenderPassSequence.h"
#include "render/RenderItem.h"
#include "render/WaterWaves.h"

class Camera;
class CausticsMap;
class CloudLayerRenderer;
class CloudShadowRenderer;
class GodRaysRenderer;
class DebugGrid;
class SelectionOutline;
class EnvironmentMap;
class EnscapeCubeRenderer;
class GBuffer;
class GpuModel;
class OpticalPathDebugRenderer;
class PostProcessor;
class RenderTarget;
class Shader;
class ShadowMap;
class SsaoRenderer;
class SpectralBeamRenderer;
class TextureCache;
class WaterSurface;

enum class GlassDebugView {
    Final = 0,
    Reflection = 1,
    Refraction = 2,
    IndexOfRefraction = 3,
    RefractedUv = 4,
    Thickness = 5,
    Transmittance = 6,
    RgbDispersion = 7,
    BackfaceThickness = 8,
    ExitSurfaceNormal = 9,
    ObjectId = 10,
    Caustics = 11,
    TransmissionShadow = 12
};

enum class CausticsMode {
    Projector = 0,
    LightSpace = 1
};

enum class RenderPath {
    Forward = 0,
    Deferred = 1
};

enum class ShadingMode {
    PhysicallyBased = 0,
    Stylized = 1
};

enum class StylizedDebugView {
    Final = 0,
    LightingBands = 1,
    Rim = 2,
    Outline = 3,
    Dither = 4,
    Fog = 5,
    ColorGrading = 6
};

enum class StylizedColorGradingLut {
    CleanToon = 0,
    Painterly = 1,
    NightAurora = 2
};

enum class StylizedPreset {
    Custom = 0,
    CleanToon = 1,
    Painterly = 2,
    NightAurora = 3
};

enum class GBufferDebugView {
    Final = 0,
    Albedo = 1,
    EncodedNormal = 2,
    MetallicRoughness = 3,
    Depth = 4,
    Ssao = 5
};

enum class LocalLightType {
    Point = 0,
    Spot = 1
};

struct LocalLight {
    glm::vec3 position{0.0f};
    float radius{3.0f};
    glm::vec3 color{1.0f};
    float intensity{8.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    float outerConeCosine{0.82f};
    LocalLightType type{LocalLightType::Point};
};

struct GpuPassTiming {
    std::string name;
    double milliseconds{0.0};
    double latestMilliseconds{0.0};
    std::size_t measurementSerial{0};
};

struct RendererSettings {
    bool enscapeCubeShaderEnabled{false};
    glm::vec3 backgroundColor{0.055f, 0.065f, 0.085f};
    glm::vec3 baseColor{1.0f};
    glm::vec3 lightDirection{-0.45f, -0.8f, -0.35f};
    float ambientStrength{0.18f};
    float diffuseStrength{0.92f};
    float specularStrength{0.28f};
    float shininess{48.0f};
    int msaaSamples{4};
    RenderPath renderPath{RenderPath::Forward};
    ShadingMode shadingMode{ShadingMode::PhysicallyBased};
    StylizedPreset stylizedPreset{StylizedPreset::Custom};
    GBufferDebugView gBufferDebugView{GBufferDebugView::Final};
    std::vector<LocalLight> localLights;
    bool wireframe{false};
    bool cullBackFaces{false};
    bool normalMapping{true};
    bool showGrid{true};
    bool showAxes{true};
    bool pbrEnabled{true};
    int stylizedBandCount{3};
    float stylizedBandSoftness{0.04f};
    float stylizedSpecularSize{0.18f};
    float stylizedSpecularSoftness{0.03f};
    float stylizedRimWidth{0.32f};
    float stylizedRimSoftness{0.08f};
    float stylizedRimIntensity{0.65f};
    glm::vec3 stylizedShadowTint{0.16f, 0.22f, 0.38f};
    glm::vec3 stylizedRimColor{0.62f, 0.82f, 1.0f};
    bool stylizedOutlineEnabled{true};
    float stylizedOutlineWidth{1.5f};
    float stylizedOutlineDepthThreshold{0.025f};
    float stylizedOutlineNormalThreshold{0.25f};
    glm::vec3 stylizedOutlineColor{0.025f, 0.035f, 0.055f};
    bool stylizedDitherEnabled{false};
    float stylizedDitherStrength{0.65f};
    bool stylizedHeightFogEnabled{false};
    float stylizedHeightFogDensity{0.16f};
    float stylizedHeightFogBaseHeight{-0.75f};
    float stylizedHeightFogFalloff{1.25f};
    glm::vec3 stylizedHeightFogColor{0.32f, 0.42f, 0.58f};
    bool stylizedColorGradingEnabled{false};
    StylizedColorGradingLut stylizedColorGradingLut{StylizedColorGradingLut::CleanToon};
    float stylizedColorGradingStrength{1.0f};
    StylizedDebugView stylizedDebugView{StylizedDebugView::Final};
    bool iblEnabled{true};
    bool shadowsEnabled{true};
    // Cascaded directional shadows. 1 cascade with the default split reproduces a single orthographic
    // box, which is the compatibility fallback for scenes written before cascades existed.
    int shadowCascadeCount{3};
    // Blend between uniform (0) and logarithmic (1) split spacing. The practical value leans
    // logarithmic because the near cascade is what runs out of resolution first.
    float shadowCascadeSplitLambda{0.75f};
    // Diagnostic palette showing which cascade receives each visible surface.
    bool shadowCascadeDebugView{false};
    WaterSettings water;
    // Sun-driven analytic sky. While enabled it *is* the environment: the skybox, the
    // irradiance and the prefiltered specular cubemaps are all rebuilt from this model, and the
    // directional light direction and colour are derived from the same sun, so the sky and the
    // light cannot disagree. Off by default, so existing scenes keep their HDR environment.
    atmosphere::AtmosphereParameters atmosphere;
    bool coloredTransmissionShadowsEnabled{true};
    bool causticsEnabled{false};
    CausticsMode causticsMode{CausticsMode::LightSpace};
    float causticsStrength{1.8f};
    float causticsScale{1.0f};
    glm::vec3 causticsDirection{0.0f};
    float causticsSharpness{0.72f};
    bool causticsAnimated{false};
    float causticsAnimationPhase{0.0f};
    float causticsReceiverPlaneY{-0.72f};
    bool transmissionEnabled{true};
    bool skyboxEnabled{true};
    int environmentPreset{0}; // 0: partly cloudy, 1: overcast, 2: clear
    bool toneMapping{true};
    bool bloom{true};
    bool showPrismIncidentBeam{false};
    bool prismOpticalPathValid{false};
    bool prismTotalInternalReflection{false};
    float environmentIntensity{0.55f};
    float refractionScale{0.18f};
    int refractionSteps{12};
    float volumeThicknessScale{1.0f};
    bool geometricThicknessEnabled{true};
    bool twoInterfaceRefractionEnabled{true};
    bool volumeGlassOverrideEnabled{false};
    float volumeGlassTransmission{1.0f};
    float volumeGlassRoughness{0.06f};
    glm::vec3 volumeGlassAttenuationColor{0.68f, 0.86f, 0.22f};
    float volumeGlassAttenuationDistance{0.85f};
    bool dispersionEnabled{true};
    float dispersionStrength{0.0f};
    GlassDebugView glassDebugView{GlassDebugView::Final};
    float exposure{1.0f};
    float bloomThreshold{1.0f};
    float bloomIntensity{0.12f};
    SpectralBeamData prismSpectrum;
    float prismBeamOutputLength{2.4f};
    float prismBeamWidth{0.045f};
    float prismBeamIntensity{5.0f};
    float prismBeamEdgeSoftness{0.65f};
    float prismBeamBloomContribution{0.35f};
    glm::vec3 prismBeamWhitePoint{1.0f};
    float indexOfRefractionOverride{0.0f};
    bool showPrismOpticalPathDebug{false};
    bool instanceOptimizationEnabled{false};
    bool frustumCullingEnabled{true};
    bool lodSelectionEnabled{true};
    float lodMediumThresholdPixels{12.0f};
    float lodHighThresholdPixels{32.0f};
    bool ssaoEnabled{false};
    float ssaoRadius{0.55f};
    float ssaoBias{0.025f};
    float ssaoStrength{1.35f};
    bool temporalAaEnabled{false};
    float temporalHistoryWeight{0.9f};
    int temporalDebugView{0};
    int skinningDebugView{0};
    bool shaderHotReloadEnabled{true};
};

class Renderer {
public:
    Renderer(
        const std::filesystem::path& vertexShaderPath,
        const std::filesystem::path& fragmentShaderPath,
        const std::filesystem::path& debugVertexShaderPath,
        const std::filesystem::path& debugFragmentShaderPath
    );
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void render(
        const std::vector<RenderItem>& renderItems,
        const Camera& camera,
        const RendererSettings& settings,
        int width,
        int height
    );

    void drawSelectionOutline(const std::vector<RenderItem>& items, const Camera& camera,
        std::uint64_t selected, bool cullBackFaces);
    unsigned int colorTexture() const;
    bool saveScreenshot(const std::filesystem::path& path, std::string& error) const;
    bool saveEditorScreenshot(
        const std::filesystem::path& path,
        int width,
        int height,
        std::string& error
    ) const;
    int activeMsaaSamples() const;
    void invalidateTemporalHistory();
    bool hasGpuFrameTime() const { return hasGpuFrameTime_; }
    double gpuFrameTimeMilliseconds() const { return gpuFrameTimeMilliseconds_; }
    double latestGpuFrameMeasurementMilliseconds() const { return latestGpuFrameMeasurementMilliseconds_; }
    std::size_t gpuFrameMeasurementSerial() const { return gpuFrameMeasurementSerial_; }
    bool hasPrismBeamGpuTime() const { return hasPrismBeamGpuTime_; }
    double prismBeamGpuTimeMilliseconds() const { return prismBeamGpuTimeMilliseconds_; }
    double latestPrismBeamMeasurementMilliseconds() const { return latestPrismBeamMeasurementMilliseconds_; }
    std::size_t prismBeamMeasurementSerial() const { return prismBeamMeasurementSerial_; }
    bool hasCausticsGpuTime() const { return hasCausticsGpuTime_; }
    double causticsGpuTimeMilliseconds() const { return causticsGpuTimeMilliseconds_; }
    double latestCausticsMeasurementMilliseconds() const { return latestCausticsMeasurementMilliseconds_; }
    std::size_t causticsMeasurementSerial() const { return causticsMeasurementSerial_; }
    std::size_t drawCallCount() const { return drawCallCount_; }
    std::size_t submittedInstanceCount() const { return submittedInstanceCount_; }
    std::size_t visibleInstanceCount() const { return visibleInstanceCount_; }
    std::size_t culledInstanceCount() const { return culledInstanceCount_; }
    const std::array<std::size_t, 3>& lodInstanceCounts() const { return lodInstanceCounts_; }
    std::size_t renderedInstanceTriangleCount() const { return renderedInstanceTriangleCount_; }
    double instancePreparationMilliseconds() const { return instancePreparationMilliseconds_; }
    const std::vector<std::string>& activePassNames() const { return activePassNames_; }
    const std::vector<RenderPassContext>& activePassContexts() const { return activePassContexts_; }
    const std::vector<GpuPassTiming>& gpuPassTimings() const { return gpuPassTimings_; }
    int shadowResolution() const;
    int renderWidth() const;
    int renderHeight() const;
    std::size_t estimatedRenderMemoryBytes() const;
    std::size_t estimatedOpaqueTrafficBytesPerFrame() const;
    // Wall time of the last analytic-sky environment rebuild, so the editor can state the real cost
    // of a cloud or sun edit instead of guessing at it.
    double environmentBuildMilliseconds() const;
    TextureCache& textureCache();
    const std::string& shaderReloadStatus() const { return shaderReloadStatus_; }
    bool shaderReloadFailed() const { return shaderReloadFailed_; }

    // The march's extinction is otherwise fixed at the calibrated constant below. This setter exists
    // for measurement, not for authoring: the shape calibration has to sweep it without a rebuild per
    // row, and `MYRENDERER_CLOUD_EXTINCTION` is the only caller.
    void setCloudMarchExtinction(float extinction) {
        cloudMarchExtinction_ = extinction > 0.0f ? extinction : 0.0f;
    }

private:
    // Only cloudless sky inputs invalidate the environment; camera/cloud changes
    // are handled by the per-frame passes and their own temporal histories.
    void updateAtmosphereEnvironment(const RendererSettings& settings);
    bool atmosphereKeyMatches(const atmosphere::AtmosphereParameters& parameters) const;
    atmosphere::AtmosphereParameters builtAtmosphere_;
    bool atmosphereActive_{false};
    int builtEnvironmentPreset_{0};
    std::unique_ptr<Shader> shader_;
    std::filesystem::path shaderDirectory_;
    std::unique_ptr<CausticsMap> causticsMap_;
    std::unique_ptr<CloudLayerRenderer> cloudLayer_;
    // The layer's extinction. A constant rather than a `RendererSettings` field because it is the
    // value `cloud::volumetricExtinction` records the calibration for -- it describes how opaque the
    // density field is, not how much of the integral is paid for, and the quality tiers below are
    // the knob for the latter. It is written out here rather than referenced because this header is
    // included by targets that do not link the cloud reference.
    //
    // The march budget itself is *not* here: it comes from `atmosphere::cloudTierBudget`, so a scene
    // that selects a tier and the renderer that draws it cannot disagree about what the tier means.
    float cloudMarchExtinction_{0.0025f};
    bool cloudHistoryInvalidated_{true};
    bool cloudPreviousDeferred_{false};
    std::unique_ptr<CloudShadowRenderer> cloudShadow_;
    std::unique_ptr<GodRaysRenderer> godRays_;
    std::unique_ptr<DebugGrid> debugGrid_;
    std::unique_ptr<SelectionOutline> selectionOutline_;
    std::unique_ptr<EnvironmentMap> environmentMap_;
    std::unique_ptr<EnscapeCubeRenderer> enscapeCubeRenderer_;
    std::unique_ptr<GBuffer> gBuffer_;
    std::unique_ptr<OpticalPathDebugRenderer> opticalPathDebugRenderer_;
    std::unique_ptr<ShadowMap> shadowMap_;
    std::unique_ptr<SsaoRenderer> ssaoRenderer_;
    std::unique_ptr<SpectralBeamRenderer> spectralBeamRenderer_;
    std::unique_ptr<Shader> shadowShader_;
    std::unique_ptr<Shader> transmissionShadowShader_;
    std::unique_ptr<Shader> glassThicknessShader_;
    std::unique_ptr<Shader> gBufferShader_;
    std::unique_ptr<Shader> deferredLightingShader_;
    std::unique_ptr<PostProcessor> postProcessor_;
    std::unique_ptr<RenderTarget> renderTarget_;
    std::unique_ptr<TextureCache> textureCache_;
    std::unique_ptr<WaterSurface> waterSurface_;
    std::unique_ptr<WaterSurface> waterSurfaceLow_;
    std::unique_ptr<Shader> waterForwardShader_;
    std::unique_ptr<Shader> waterGBufferShader_;
    float previousWaterTime_{0.0f};
    bool previousWaterValid_{false};
    bool enscapeCubeActive_{false};
    unsigned int fullscreenVertexArray_{0};
    std::array<unsigned int, 4> timingQueries_{};
    std::array<bool, 4> timingQueryPending_{};
    std::array<unsigned int, 4> beamStartQueries_{};
    std::array<unsigned int, 4> beamEndQueries_{};
    std::array<bool, 4> beamTimingPending_{};
    std::array<unsigned int, 4> causticsStartQueries_{};
    std::array<unsigned int, 4> causticsEndQueries_{};
    std::array<bool, 4> causticsTimingPending_{};
    static constexpr std::size_t maxProfiledPasses_ = 16U;
    static constexpr std::size_t passTimingRingSize_ = 4U;
    std::array<unsigned int, maxProfiledPasses_ * passTimingRingSize_> passStartQueries_{};
    std::array<unsigned int, maxProfiledPasses_ * passTimingRingSize_> passEndQueries_{};
    std::array<bool, maxProfiledPasses_ * passTimingRingSize_> passTimingPending_{};
    std::array<std::string, maxProfiledPasses_ * passTimingRingSize_> passTimingNames_{};
    std::array<std::size_t, maxProfiledPasses_> nextPassTimingRing_{};
    std::size_t nextTimingQuery_{0};
    std::size_t nextBeamTimingQuery_{0};
    std::size_t nextCausticsTimingQuery_{0};
    double gpuFrameTimeMilliseconds_{0.0};
    double prismBeamGpuTimeMilliseconds_{0.0};
    double causticsGpuTimeMilliseconds_{0.0};
    double latestGpuFrameMeasurementMilliseconds_{0.0};
    double latestPrismBeamMeasurementMilliseconds_{0.0};
    double latestCausticsMeasurementMilliseconds_{0.0};
    std::size_t gpuFrameMeasurementSerial_{0};
    std::size_t prismBeamMeasurementSerial_{0};
    std::size_t causticsMeasurementSerial_{0};
    std::size_t drawCallCount_{0};
    std::size_t submittedInstanceCount_{0};
    std::size_t visibleInstanceCount_{0};
    std::size_t culledInstanceCount_{0};
    std::array<std::size_t, 3> lodInstanceCounts_{};
    std::size_t renderedInstanceTriangleCount_{0};
    double instancePreparationMilliseconds_{0.0};
    bool hasGpuFrameTime_{false};
    bool hasPrismBeamGpuTime_{false};
    bool hasCausticsGpuTime_{false};
    std::vector<std::string> activePassNames_;
    std::vector<RenderPassContext> activePassContexts_;
    std::vector<GpuPassTiming> gpuPassTimings_;
    RenderPath activeRenderPath_{RenderPath::Forward};
    glm::mat4 previousViewProjection_{1.0f};
    std::size_t temporalFrameIndex_{0U};
    int lastTemporalWidth_{0};
    int lastTemporalHeight_{0};
    bool previousViewProjectionValid_{false};
    bool lastTemporalAaEnabled_{false};
    std::size_t shaderReloadPollFrame_{0U};
    std::string shaderReloadStatus_{"Watching shader files"};
    bool shaderReloadFailed_{false};
    OpenGlStateCache stateCache_;
};
