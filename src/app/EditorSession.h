#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "runtime/Timeline.h"

enum class EditorRenderBackend {
    Raster = 0,
    CpuPathTraced,
    GpuPathTraced
};

enum class EditorActivity {
    Edit = 0,
    Preview,
    Bake,
    Render
};

enum class EditorCommandType {
    BackendChanged,
    ActivityChanged,
    PauseChanged,
    Step,
    Reset,
    RenderFrame,
    RenderSequence,
    SubmitRenderJob,
    CancelRenderJob,
    MoveRenderJobUp,
    MoveRenderJobDown,
    RemoveRenderJob,
    RetryRenderJob,
    RefreshAssetCatalog,
    OpenSceneAsset,
    ImportModelAsset,
    SelectRenderJobAsset,
    SetEntityTransform,
    SetEntityTint,
    SetEntityCastsShadow,
    SetStageSettings,
    SetMaterialSettings,
    SetDirectionalLightSettings,
    SetPbrEnvironmentSettings,
    SetWaterSettings,
    SetShadingSettings,
    SetPostProcessingSettings,
    SetRenderPluginEnabled,
    SetRenderPipelineMode,
    SetRasterizationSettings,
    SetCameraSettings,
    SetRuntimeSettings,
    SetCpuPreviewSettings,
    RestartCpuPreview,
    NewScene,
    OpenSceneDialog,
    SaveScene,
    SaveSceneAs,
    ResetSceneModel,
    ApplyScenePreset,
    SetGlassSettings,
    SetCausticsSettings,
    SetInstanceSettings,
    SetAtmosphereSettings,
    FrameCamera,
    SetActiveModule,
    SetModuleParameter,
    SetModuleSeed,
    DuplicateEntity,
    DeleteEntity,
    SetEntityVisibility,
    SetEntityParent
};

enum class EditorCameraFrameTarget {
    Default = 0,
    Selection,
    Model
};

struct EditorVector3Payload {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

struct EditorTransformPayload {
    EditorVector3Payload translation;
    EditorVector3Payload rotationDegrees;
    EditorVector3Payload scale{1.0f, 1.0f, 1.0f};
};

struct EditorStageSettingsPayload {
    bool groundReceiver{true};
    EditorVector3Payload groundColor{0.58f, 0.60f, 0.64f};
    float groundOffset{-0.72f};
    bool comparisonObject{false};
};

struct EditorMaterialSettingsPayload {
    EditorVector3Payload baseColor{1.0f, 1.0f, 1.0f};
    float shininess{48.0f};
};

struct EditorDirectionalLightSettingsPayload {
    EditorVector3Payload direction{-0.45f, -0.8f, -0.35f};
    float ambientStrength{0.18f};
    float diffuseStrength{0.92f};
    float specularStrength{0.28f};
};

// PBR / IBL / visibility toggles plus the environment intensity. Every field
// feeds the CPU reference snapshot as well as the raster and deferred paths, so
// this domain restarts the CPU preview and drops temporal history together.
struct EditorPbrEnvironmentSettingsPayload {
    bool pbrEnabled{true};
    bool iblEnabled{true};
    bool skyboxEnabled{true};
    int environmentPreset{0};
    bool shadowsEnabled{true};
    int shadowCascadeCount{3};
    float shadowCascadeSplitLambda{0.75f};
    bool shadowCascadeDebugView{false};
    bool coloredTransmissionShadowsEnabled{true};
    float environmentIntensity{0.55f};
};

struct EditorWaterSettingsPayload {
    bool enabled{false};
    int preset{0};
    int quality{1};
    float level{-0.45f};
    float extent{110.0f};
    float amplitude{0.22f};
    float speed{1.0f};
    float steepness{0.65f};
    float foamStrength{0.7f};
    float roughness{0.25f};
    float reflectionStrength{1.0f};
    float rippleStrength{0.0f};
    float sunGlintStrength{1.0f};
    float deepWaterStrength{0.0f};
    float waveDiversity{0.0f};
    float nearMeshFocus{0.0f};
    bool surfaceOptics{false};
    float cloudReflectionStrength{0.0f};
    float windX{0.9f};
    float windZ{0.3f};
};

// Opaque shading mode, render path, G-buffer debug view and the stylized / NPR
// parameters. These are raster-only: the CPU reference integrator consumes the
// physical material and light semantics instead, so the domain invalidates
// temporal history without restarting the CPU preview.
struct EditorShadingSettingsPayload {
    int shadingMode{0};
    int renderPath{0};
    int gBufferDebugView{0};
    int stylizedBandCount{3};
    float stylizedBandSoftness{0.04f};
    float stylizedSpecularSize{0.18f};
    float stylizedSpecularSoftness{0.03f};
    float stylizedRimWidth{0.32f};
    float stylizedRimSoftness{0.08f};
    float stylizedRimIntensity{0.65f};
    EditorVector3Payload stylizedShadowTint{0.16f, 0.22f, 0.38f};
    EditorVector3Payload stylizedRimColor{0.62f, 0.82f, 1.0f};
    bool stylizedOutlineEnabled{true};
    float stylizedOutlineWidth{1.5f};
    float stylizedOutlineDepthThreshold{0.025f};
    float stylizedOutlineNormalThreshold{0.25f};
    EditorVector3Payload stylizedOutlineColor{0.025f, 0.035f, 0.055f};
    bool stylizedDitherEnabled{false};
    float stylizedDitherStrength{0.65f};
    bool stylizedHeightFogEnabled{false};
    float stylizedHeightFogDensity{0.16f};
    float stylizedHeightFogBaseHeight{-0.75f};
    float stylizedHeightFogFalloff{1.25f};
    EditorVector3Payload stylizedHeightFogColor{0.32f, 0.42f, 0.58f};
    bool stylizedColorGradingEnabled{false};
    int stylizedColorGradingLut{0};
    float stylizedColorGradingStrength{1.0f};
    int stylizedDebugView{0};
};

// SSAO, temporal AA and the tone mapping / bloom block. SSAO and TAA feed the
// resolved HDR scene that history reprojection reuses, so this domain drops
// temporal history while leaving the CPU preview untouched.
struct EditorPostProcessingSettingsPayload {
    bool ssaoEnabled{false};
    float ssaoRadius{0.55f};
    float ssaoBias{0.025f};
    float ssaoStrength{1.35f};
    bool temporalAaEnabled{false};
    float temporalHistoryWeight{0.9f};
    int temporalDebugView{0};
    bool toneMapping{true};
    bool bloom{true};
    float bloomThreshold{1.0f};
    float bloomIntensity{0.12f};
    float exposure{1.0f};
};

struct EditorRasterizationSettingsPayload {
    bool wireframe{false};
    bool cullBackFaces{false};
    bool normalMapping{true};
    bool showGrid{true};
    bool showAxes{true};
    EditorVector3Payload backgroundColor{0.055f, 0.065f, 0.085f};
    int msaaSamples{4};
};

// Orbit camera field of view. The CPU preview signature covers the projection,
// so this domain restarts the preview as well as dropping temporal history.
struct EditorCameraSettingsPayload {
    float fieldOfViewDegrees{45.0f};
    float farPlane{100.0f};
};

// Window level runtime state that does not participate in either the raster
// image or the reference snapshot.
struct EditorRuntimeSettingsPayload {
    bool vsync{true};
    bool shaderHotReloadEnabled{true};
};

// Preview task configuration; these settings are not part of a saved Scene.
struct EditorCpuPreviewSettingsPayload {
    int scaleMode{0};
    int samplesPerPixel{64};
    int maxDepth{6};
    int seed{1};
    int output{0};
    bool denoise{true};
    int atrousIterations{4};
    bool temporalDenoise{true};
    bool fireflyClamp{false};
    bool powerWeightedLights{true};
    bool ggxVndf{true};
};

struct EditorGlassSettingsPayload {
    bool transmissionEnabled{true};
    bool dispersionEnabled{true};
    bool geometricThicknessEnabled{true};
    bool twoInterfaceRefractionEnabled{true};
    float refractionScale{0.18f};
    int refractionSteps{12};
    float volumeThicknessScale{1.0f};
    bool volumeGlassOverrideEnabled{false};
    float volumeGlassTransmission{1.0f};
    float volumeGlassRoughness{0.06f};
    EditorVector3Payload volumeGlassAttenuationColor{0.68f, 0.86f, 0.22f};
    float volumeGlassAttenuationDistance{0.85f};
    float dispersionStrength{0.0f};
    int glassDebugView{0};
};

struct EditorCausticsSettingsPayload {
    bool causticsEnabled{false};
    int causticsMode{1};
    float causticsStrength{1.8f};
    float causticsScale{1.0f};
    EditorVector3Payload causticsDirection{0.0f, 0.0f, 0.0f};
    float causticsSharpness{0.72f};
    bool causticsAnimated{false};
};

struct EditorInstanceSettingsPayload {
    bool instanceOptimizationEnabled{false};
    bool frustumCullingEnabled{true};
    bool lodSelectionEnabled{true};
};

// Analytic Rayleigh/Mie sky. One sun position drives the environment cubemap, the
// directional light and the shadow map, so this domain rebuilds the environment and
// restarts the CPU preview as well as dropping temporal history.
struct EditorAtmosphereSettingsPayload {
    bool enabled{false};
    float sunElevationDegrees{35.0f};
    float sunAzimuthDegrees{135.0f};
    float turbidity{1.0f};
    float skyIntensity{1.0f};
    float sunIntensity{1.0f};
    float groundAlbedo{0.10f};
    bool nightSkyEnabled{false};
    float moonIntensity{1.0f};
    float starIntensity{1.0f};
    bool aerialPerspectiveEnabled{false};
    float aerialPerspectiveStrength{1.0f};
    float aerialPerspectiveScaleHeight{60.0f};
    // The cloud layer belongs to the sky domain: it is composited into the same environment
    // cubemap and shares the same sun, so editing it must invalidate exactly what editing the sky
    // invalidates.
    bool cloudsEnabled{false};
    float cloudBaseHeight{3200.0f};
    float cloudTopHeight{8000.0f};
    float cloudCoverage{0.50f};
    float cloudDensity{1.0f};
    float cloudWindOffsetX{0.0f};
    float cloudWindOffsetZ{0.0f};
    float cloudFeatureScale{6400.0f};
    float cloudNoisePeriod{4.0f};
    // The 2D weather map's controls (C5). R is coverage, G is cloud type, B is height; these are the
    // layer-wide values those channels swing around.
    float cloudWeatherScale{25600.0f};
    float cloudCoverageVariation{0.90f};
    float cloudType{0.65f};
    float cloudTypeVariation{0.50f};
    float cloudHeightVariation{0.30f};
    float cloudDetailStrength{0.45f};
    float cloudDetailEdge{0.15f};
    bool cloudHeightLighting{false};
    float cloudShapeBlend{0.0f};
    // `atmosphere::CloudQualityTier`, carried as an int so this payload stays a plain aggregate the
    // command queue can copy without the optics header. The Inspector is the only writer and it
    // writes values the enum defines.
    int cloudQuality{0};
    bool cloudHalfResolution{false};
    bool cloudTemporalEnabled{false};
    bool cloudShadowsEnabled{false};
    bool cloudGodRaysEnabled{false};
    bool cloudDeterministic{false};
    bool cloudOfflineNoise{false};
    float cloudGodRaysStrength{0.08f};
    float cloudHorizonFadeDegrees{6.0f};
    float cloudAmbientElevationDegrees{15.0f};
    float cloudAmbientScale{0.85f};
};

// One module parameter edit. `type` mirrors ModuleParameterType, and the colour of a
// Color parameter travels in the command's shared colour payload.
struct EditorModuleParameterPayload {
    int type{0};
    bool boolean{false};
    int integer{0};
    float number{0.0f};
    std::string text;
};

// One command from the editor UI to the central entry point.
//
// Every domain payload below is filled in by name after construction
// (`command.stage.groundReceiver = ...`), so call sites only initialise the command
// identity. These constructors exist so that stays the case: aggregate
// brace-initialisation with omitted members is legal C++ (the remaining members use
// their default member initializers), but GCC's `-Wextra` reports one
// `-Wmissing-field-initializers` warning per omitted member, which would drown a
// MinGW build in noise once the payload set grew.
struct EditorCommand {
    EditorCommand() = default;
    explicit EditorCommand(EditorCommandType commandType) : type(commandType) {}
    EditorCommand(
        EditorCommandType commandType,
        std::uint64_t entityId,
        std::uint64_t valueId = 0U,
        bool flagValue = false
    ) : type(commandType), entity(entityId), value(valueId), flag(flagValue) {}

    EditorCommandType type{EditorCommandType::Reset};
    std::uint64_t entity{0U};
    std::uint64_t value{0U};
    bool flag{false};
    std::string text;
    EditorTransformPayload transform;
    EditorVector3Payload color;
    EditorStageSettingsPayload stage;
    EditorMaterialSettingsPayload material;
    EditorDirectionalLightSettingsPayload directionalLight;
    EditorPbrEnvironmentSettingsPayload pbrEnvironment;
    EditorWaterSettingsPayload water;
    EditorShadingSettingsPayload shading;
    EditorPostProcessingSettingsPayload postProcessing;
    EditorRasterizationSettingsPayload rasterization;
    EditorCameraSettingsPayload camera;
    EditorRuntimeSettingsPayload runtime;
    EditorCpuPreviewSettingsPayload cpuPreview;
    EditorGlassSettingsPayload glass;
    EditorCausticsSettingsPayload caustics;
    EditorInstanceSettingsPayload instance;
    EditorAtmosphereSettingsPayload atmosphere;
    EditorModuleParameterPayload moduleParameter;
};

class EditorSession {
public:
    void requestBackend(EditorRenderBackend backend);
    void requestActivity(EditorActivity activity);
    void requestPause(bool paused);
    void request(EditorCommand command);
    std::vector<EditorCommand> takeCommands();

    EditorRenderBackend backend() const { return backend_; }
    EditorActivity activity() const { return activity_; }
    bool paused() const { return paused_; }

    // Deterministic frame/time state. The editor and the headless batch runtime share
    // this one Timeline definition, so a GUI frame, a single step and a rendered
    // sequence frame cannot disagree about time.
    const Timeline& timeline() const { return timeline_; }
    Timeline& timeline() { return timeline_; }
    int frame() const { return timeline_.frame(); }
    int startFrame() const { return timeline_.startFrame(); }
    int endFrame() const { return timeline_.endFrame(); }
    int framesPerSecond() const { return timeline_.framesPerSecond(); }
    void setFrame(int frame);
    void setFrameRange(int startFrame, int endFrame);
    void setFramesPerSecond(int framesPerSecond);
    double timeSeconds() const;

    void setTaskStatus(std::string status) { taskStatus_ = std::move(status); }
    const std::string& taskStatus() const { return taskStatus_; }

private:
    EditorRenderBackend backend_{EditorRenderBackend::Raster};
    EditorActivity activity_{EditorActivity::Edit};
    bool paused_{false};
    Timeline timeline_;
    std::string taskStatus_{"Idle"};
    std::deque<EditorCommand> commands_;
};
