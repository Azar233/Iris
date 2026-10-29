#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <deque>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <unordered_set>

namespace iris { struct PluginControlBounds; }

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "io/ModelImporter.h"
#include "app/EditorSession.h"
#include "app/WorkspaceAssets.h"
#include "app/WorkspaceShaderPackage.h"
#include "render/ShaderSourceMap.h"
#include "app/AssetThumbnail.h"
#include "module/ModuleRegistry.h"
#include "module/ModuleRuntime.h"
#include "optics/PrismDemo.h"
#include "pathtracer/ProgressiveRenderer.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "runtime/RenderQueue.h"
#include "scene/Scene.h"
#include "scene/SceneDocument.h"

struct GLFWwindow;
class GpuModel;
class ModelImporter;
class Renderer;
class Shader;

enum class VolumeGlassPreset {
    Clear = 0,
    Olive,
    Amber,
    Crystal
};

class Application {
public:
    explicit Application(bool offlineCapture = false);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(const std::filesystem::path& initialModel = {});
    int runRasterSequence(const RenderJob& job, const std::string& cancelEvent = {});

private:
    void initializeWindow();
    void initializeGui();
    void initializeRenderer();
    void initializeImporters();
    void shutdown();

    void drawMainMenu();
    void drawWorkspaceToolbar();
    void processEditorCommands();
    void submitRenderJob(const std::filesystem::path& path);
    void updateRenderQueue();
    void cancelRenderJob(std::uint64_t id = 0U);
    void applyVsync(bool enabled);
    void drawScenePanel();
    void drawAssetsPanel();
    void drawShaderPackageInspector();
    bool recompileWorkspaceShaderScene();
    void drawEditorLayout();
    void newEmptyScene();
    SceneDocument captureSceneDocument() const;
    bool saveCurrentScene();
    bool saveSceneAs();
    bool saveSceneTo(const std::filesystem::path& path);
    bool openScene(const std::filesystem::path& path);
    void openSceneFromDialog();
    void rememberRecentScene(const std::filesystem::path& path);
    std::filesystem::path recentScenePath() const;
    void deleteSelectedEntity();
    void selectEntity(SceneEntityId id);
    SceneEntityId pickEntity(const std::vector<RenderItem>& items, int width, int height, int x, int y);
    void materializeStressEntities(std::vector<RenderItem>& items);
    bool editorInteractionRegression();
    void runPhysicsWorkbenchAcceptance();
    int physicsUiPhase_{0};
    bool physicsUiChanged_{false};
    void interactPhysicsControl(bool valueChanged);
    int physicsTemporalFrame_{0};
    void advancePhysicsTemporalAcceptance();
    void capturePhysicsTemporalAcceptance();
    bool pluginActivationRegression();
    bool pluginParameterRegression();
    bool waterPluginRegression();
    void interactWaterParameters(const std::vector<iris::PluginControlBounds>& controls);
    bool nprRegression();
    void interactNprParameters(const std::vector<iris::PluginControlBounds>& controls);
    bool shaderTransactionAcceptance();
    void drawInspectorPanel();
    void drawViewportPanel();
    void drawBufferPreviewPanel();
    bool runLightEntityAcceptance(const std::filesystem::path& directory);
    void runShaderSceneAcceptance(const std::filesystem::path& directory);
    void runShaderSourceMapAcceptance(const std::filesystem::path& directory);
    void runShaderParameterAcceptance(const std::filesystem::path& directory);
    void runExternalShaderPackageAcceptance(const std::filesystem::path& directory);
    void runShaderPackageReloadAcceptance(const std::filesystem::path& directory);
    void runShaderHistoryAcceptance(const std::filesystem::path& directory);
    void runShaderSequenceAcceptance(const std::filesystem::path& directory);
    void drawShaderRuntimeInspector();
    void drawShaderMaterialInspector(const SceneEntity& entity);
    void runShaderMaterialAcceptance(const std::filesystem::path& directory);
    void runShaderMaterialWorkspaceAcceptance(const std::filesystem::path& directory);
    bool finishShaderMaterialWorkspaceAcceptance();
    void interactShaderMaterialWorkspace(int phase, int frame);
    void runEnscapeShaderSceneAcceptance(const std::filesystem::path& directory);
    void updateViewportCameraNavigation();
    void drawModulePanel();
    void drawRigidBodyInspector(const SceneEntity& entity);
    // Aggregates the structured diagnostics the application already owns: import
    // diagnostics, render task history, frame/pass profile and the module log. It never
    // invents a state that no subsystem reported.
    void drawLogProfilePanel();
    // Drives the active module against the discardable runtime scene for the current
    // frame and keeps the Viewport on that scene. Never writes back to the edit scene.
    void updateModulePreview();
    void synchronizeModulePlayback();
    bool modulePreviewEnabled() const { return !activeModuleId_.empty(); }
    const Scene& viewportScene() const;
    // Automation entry point for the module preview: `MYRENDERER_MODULE`,
    // `MYRENDERER_MODULE_SEED` and `MYRENDERER_TIMELINE_FRAME`.
    void applyModuleEnvironmentOverrides();
    std::uint64_t cpuPreviewInputSignature(int width, int height) const;
    void updateCpuPreview(int width, int height);
    void uploadCpuPreviewTexture();
    void exportCpuPreview();
    EditorCpuPreviewSettingsPayload cpuPreviewSettings() const;
    void captureReferenceComparison(int width, int height);
    void drawOrientationGizmo();
    void drawAboutPopup();
    void drawDiagnostics();

    void discoverModels();
    void updateWorkspaceAssetViews();
    void updateAssetThumbnail();
    void requestAssetThumbnail(const WorkspaceAssetRecord& asset);
    bool loadModel(const std::filesystem::path& path, bool append = false);
    void updateModelLoad();
    void finishModelLoad(const std::filesystem::path& path, ModelImportResult loaded, bool append);
    void queueDroppedFiles(int count, const char** paths);
    const ModelImporter* findImporter(const std::filesystem::path& path) const;
    std::filesystem::path resolvePath(const std::filesystem::path& path) const;
    std::filesystem::path nextScreenshotPath() const;
    void resetObjectTransform();
    void rebuildSceneEntities();
    void syncSceneEntities(const glm::mat4& normalization);
    void activatePrismDemoPreset(bool loadFixture);
    void activateGlassCausticsPreset();
    void applyStylizedPreset(StylizedPreset preset);
    void applyVolumeGlassPreset(VolumeGlassPreset preset);
    void activateLightStressPreset(bool loadFixture);
    void activateInstanceStressPreset(bool loadFixture);
    void rebuildLocalLights();
    void deactivatePrismDemoPreset();
    void updatePrismDemoOptics();
    void applyPrismOpticalPreset(PrismOpticalPreset preset);
    void restorePrismHeroShot();
    void writePrismBenchmarkReport();
    void updatePrismReelFrame();

    GLFWwindow* window_{nullptr};
    bool guiInitialized_{false};
    bool rasterSequenceMode_{false};
    bool shutdownComplete_{false};

    std::unique_ptr<Renderer> renderer_;
    // `MYRENDERER_CLOUD_EXTINCTION`. Read in the constructor, applied in `initializeRenderer`,
    // because the renderer owns the value and does not exist yet when the environment is parsed.
    bool cloudMarchExtinctionOverridden_{false};
    float cloudMarchExtinctionOverride_{0.0f};
    std::unique_ptr<GpuModel> model_;
    std::vector<std::unique_ptr<GpuModel>> importedModels_;
    bool resetEditorLayout_{false};
    bool emptySceneSession_{false};
    bool loadedSceneDocument_{false};
    bool focusObjectTab_{false};
    bool focusRendererTab_{false};
    bool focusBuffersTab_{false};
    std::filesystem::path pendingBufferExportDirectory_;
    int bufferExportWarmup_{3};
    bool bufferDrawerInteractionComplete_{false};
    int lightUiInteractionPhase_{0};
    bool lightUiInteractionComplete_{false};
    std::filesystem::path shaderTransactionSources_;
    std::filesystem::path shaderTransactionOutput_;
    int shaderRetryInteractionPhase_{0};
    bool shaderRetryInteractionComplete_{false};
    bool focusPluginsTab_{false};
    int pendingGlslControlsScrollFrames_{0};
    bool focusAssetsTab_{false};
    bool focusRenderQueueTab_{false};
    bool focusModulesTab_{false};
    bool focusModuleTab_{false};
    bool focusLogTab_{false};
    EditorSession editorSession_;
    // Statically linked module set. The Modules panel and the Content Browser read
    // manifests from here; the runtime creates instances by stable id.
    ModuleRegistry moduleRegistry_{createBuiltinModuleRegistry()};
    ModuleRuntime moduleRuntime_{moduleRegistry_};
    // Module preview state: the selection and its parameter overrides are the module's
    // input identity, so any change rebuilds the runtime scene from the edit scene.
    std::string activeModuleId_;
    std::vector<ModuleParameterOverride> moduleParameterOverrides_;
    std::uint32_t moduleSeed_{20260919U};
    std::uint64_t moduleInputRevision_{0U};
    std::uint64_t moduleBuiltRevision_{0U};
    std::uint64_t moduleBuiltSceneGeneration_{0U};
    std::size_t moduleBuiltEntityCount_{0U};
    std::string moduleMessage_{"No module is active."};
    bool showPhysicsColliders_{false};
    std::unique_ptr<RenderQueue> renderQueue_;
    std::array<char, 1024> renderJobPathBuffer_{};
    std::string renderQueueMessage_{"No Render Job submitted."};
    std::array<char, 128> contentSearch_{};
    int contentCategory_{0};
    std::string contentExtensionFilter_;
    int contentSortMode_{0};
    bool contentGridView_{true};
    bool contentFoldersExpanded_{true};
    std::filesystem::path selectedWorkspaceAsset_;
    WorkspaceAssetCatalog workspaceAssets_;
    bool scrollWorkspaceSelection_{false};
    WorkspaceShaderPackage workspaceShaderPackage_;
    std::string shaderWorkspaceMessage_{"No workspace compile requested"};
    std::string shaderWorkspaceFileMessage_;
    std::vector<ShaderSourceDiagnostic> shaderWorkspaceDiagnostics_;
    std::vector<ShaderSourceMap> shaderWorkspaceSourceMaps_;
    int inspectedShaderLine_{0};
    std::vector<std::string> shaderDiagnosticContext_;
    int shaderDiagnosticUiPhase_{0};
    int shaderParameterUiPhase_{0};
    bool shaderParameterUiFloatChanged_{false}, shaderParameterUiBoolChanged_{false};
    int shaderWorkspaceUiPhase_{0};
    int shaderWorkspaceCompileAttempts_{0};
    bool shaderWorkspaceLastCompileAccepted_{false};
    std::uint64_t workspaceShaderGeneration_{0};
    std::uint64_t workspaceShaderRuntimeRevision_{0};
    int shaderHistoryUiPhase_{0};
    int shaderMouseUiPhase_{0};
    bool shaderHistoryUiPassed_{false};
    std::filesystem::path inspectedShaderFile_;
    int shaderBrowserInteractionPhase_{0};
    int shaderBrowserUiFrames_{0};
    std::uint64_t shaderBrowserInitialGeneration_{0};
    std::uint64_t thumbnailCacheGeneration_{0U};
    struct UploadedThumbnail {
        std::uint64_t key{0U};
        unsigned int texture{0U};
        std::string error;
    };
    struct PendingThumbnail {
        std::filesystem::path path;
        std::uint64_t key{0U};
        AssetThumbnail image;
    };
    std::map<std::filesystem::path, std::uint64_t> thumbnailKeys_;
    std::map<std::filesystem::path, UploadedThumbnail> uploadedThumbnails_;
    unsigned int editorIconTexture_{0U};
    bool editorIconLoadAttempted_{false};
    std::future<PendingThumbnail> pendingThumbnail_;
    std::uint64_t sceneGeneration_{0};
    std::unique_ptr<Shader> pickingShader_;
    std::vector<SceneEntityId> stressEntities_;
    std::unordered_set<SceneEntityId> editedEntities_;
    std::unique_ptr<GpuModel> groundModel_;
    std::unique_ptr<GpuModel> glassBackdropModel_;
    std::vector<std::unique_ptr<ModelImporter>> importers_;
    Camera camera_;
    RendererSettings rendererSettings_;
    Scene scene_;
    SceneEntityId primaryEntity_{invalidSceneEntityId};
    SceneEntityId comparisonEntity_{invalidSceneEntityId};
    SceneEntityId backdropEntity_{invalidSceneEntityId};
    SceneEntityId groundEntity_{invalidSceneEntityId};
    SceneEntityId selectedSceneEntity_{invalidSceneEntityId};
    bool hideSelectionOutlineForAutomation_{false};
    std::vector<SceneEntityId> foundationDemoEntities_;

    std::filesystem::path sourceRoot_;
    std::filesystem::path currentModelPath_;
    std::filesystem::path currentScenePath_;
    std::filesystem::path pendingScreenshotPath_;
    std::filesystem::path pendingEditorScreenshotPath_;
    // `MYRENDERER_CPU_PREVIEW_EXPORT`: waits until the preview reaches its target SPP,
    // writes the same reference PNG the CLI writes and then closes, so a GUI frame and a
    // batch frame can be compared byte for byte.
    std::filesystem::path pendingCpuPreviewExportPath_;
    int pendingScreenshotWarmupFrames_{0};
    bool presentationSnapshotCaptured_{false};
    int pendingEditorScreenshotWarmupFrames_{3};
    // `MYRENDERER_EDITOR_SCREENSHOT_SCROLL`: Inspector scroll offset used while an automated
    // editor capture is pending, so a capture can reach a section below the fold.
    float pendingEditorScreenshotScroll_{0.0f};
    std::filesystem::path benchmarkOutputPath_;
    std::filesystem::path prismReelFramesDirectory_;
    std::filesystem::path referenceComparisonDirectory_;
    std::vector<std::filesystem::path> availableModels_;
    std::vector<std::filesystem::path> availableScenes_;
    std::array<char, 1024> modelPathBuffer_{};
    std::array<char, 4096> shaderMaterialPathBuffer_{};
    SceneEntityId shaderMaterialInspectorEntity_{invalidSceneEntityId};
    int shaderMaterialWorkspacePhase_{0};
    int waterUiPhase_{0};
    int waterUiFrames_{0};
    float waterUiInitialAmplitude_{0.f};
    int nprUiPhase_{0};
    int nprUiFrames_{0};
    float nprUiInitialValue_{0.0f};
    std::filesystem::path shaderMaterialWorkspaceDirectory_;
    std::size_t shaderMaterialWorkspaceEntityCount_{0};
    int shaderMaterialUiFrames_{0};
    int shaderMaterialUiPhase_{0};
    bool shaderMaterialUiEdited_{false};
    bool shaderMaterialUiCleared_{false};
    std::string statusMessage_{"Ready"};
    std::string gpuDescription_;
    std::vector<ModelDiagnostic> modelDiagnostics_;

    struct PendingModelImport {
        std::filesystem::path path;
        std::future<ModelImportResult> future;
        std::chrono::steady_clock::time_point startedAt;
        std::uintmax_t fileSize{0};
        bool append{false};
        std::uint64_t generation{0};
    };
    std::optional<PendingModelImport> pendingModelImport_;
    std::deque<std::filesystem::path> droppedModelPaths_;

    struct PrismDemoPreviousState {
        RendererSettings rendererSettings;
        bool autoRotate{false};
        bool showGroundPlane{true};
        bool showComparisonObject{false};
    };
    std::optional<PrismDemoPreviousState> prismDemoPreviousState_;

    glm::vec3 modelCenter_{0.0f};
    glm::vec3 modelPosition_{0.0f};
    glm::vec3 modelRotationDegrees_{0.0f};
    float modelNormalizationScale_{1.0f};
    float modelScale_{1.0f};
    glm::vec3 groundColor_{0.58f, 0.60f, 0.64f};
    float groundOffset_{-0.72f};
    std::size_t loadedMeshCount_{0};
    std::size_t loadedSubmeshCount_{0};
    std::size_t loadedTransparentSubmeshCount_{0};
    std::size_t loadedVertexCount_{0};
    std::size_t loadedTriangleCount_{0};
    std::size_t loadedMaterialCount_{0};
    std::size_t loadedTextureCount_{0};
    std::size_t loadedDecodedTextureCount_{0};
    std::size_t loadedFallbackTextureCount_{0};
    std::size_t loadedTextureMemoryBytes_{0};
    std::size_t unsupportedModelCount_{0};
    double cpuFrameTimeMilliseconds_{0.0};
    double lastCpuImportMilliseconds_{0.0};
    double lastGpuUploadMilliseconds_{0.0};
    double lastLoadTotalMilliseconds_{0.0};
    std::vector<double> benchmarkCpuFrameTimes_;
    std::vector<double> benchmarkGpuFrameTimes_;
    std::vector<double> benchmarkActiveCpuFrameTimes_, benchmarkCompletedCpuFrameTimes_;
    std::map<std::string, std::vector<double>> benchmarkVulkanCounters_;
    std::vector<std::pair<std::string,double>> benchmarkFirstFrameCpuPasses_;
    double benchmarkStartupMilliseconds_{0}, benchmarkFirstFrameMilliseconds_{0};
    std::vector<std::string> benchmarkFrameTrace_;
    double benchmarkPtTargetFromRunMilliseconds_{0}, benchmarkPtTargetGenerationMilliseconds_{0};
    std::vector<double> benchmarkBeamGpuTimes_;
    std::vector<double> benchmarkCausticsGpuTimes_;
    std::map<std::string, std::vector<double>> benchmarkPassGpuTimes_;
    std::map<std::string, std::vector<double>> benchmarkPassCpuTimes_;
    std::map<std::string, std::size_t> lastBenchmarkPassSerials_;
    std::size_t lastBenchmarkGpuFrameSerial_{0};
    std::size_t lastBenchmarkBeamSerial_{0};
    std::size_t lastBenchmarkCausticsSerial_{0};
    int renderWidthOverride_{0};
    int renderHeightOverride_{0};
    int benchmarkWarmupFrames_{60};
    int benchmarkMeasurementFrames_{180};
    int benchmarkRenderedFrames_{0};
    int prismReelFrameIndex_{0};
    int prismReelFrameCount_{360};
    int prismReelWarmupFrames_{8};
    int referenceComparisonWarmupFrames_{2};
    std::uint32_t referenceComparisonSamples_{128U};
    std::uint32_t referenceComparisonMaxDepth_{8U};
    std::uint32_t referenceComparisonSeed_{20260915U};

    pathtracer::RenderTask cpuPreviewTask_;
    std::shared_ptr<const pathtracer::RenderProgress> cpuPreviewProgress_;
    std::uint64_t cpuPreviewObservedSignature_{0U};
    std::uint64_t cpuPreviewTaskSignature_{0U};
    std::uint64_t cpuPreviewTaskId_{0U};
    std::uint64_t cpuPreviewUploadedTaskId_{0U};
    std::uint32_t cpuPreviewUploadedSamples_{0U};
    std::chrono::steady_clock::time_point cpuPreviewLastInputChange_{};
    unsigned int cpuPreviewTexture_{0U};
    int cpuPreviewTextureWidth_{0};
    int cpuPreviewTextureHeight_{0};
    int viewportRenderMode_{0}; // 0 Raster, 1 CPU Path Traced.
    int cpuPreviewScaleMode_{0}; // 0 Auto, 1 1/4, 2 1/2, 3 Full.
    int cpuPreviewOutput_{0};
    int cpuPreviewSamplesPerPixel_{64};
    int cpuPreviewMaxDepth_{6};
    int cpuPreviewSeed_{1};
    bool cpuPreviewDenoise_{true};
    bool cpuPreviewTemporalDenoise_{true};
    bool cpuPreviewFireflyClamp_{false};
    int cpuPreviewAtrousIterations_{4};
    bool cpuPreviewPowerWeightedLights_{true};
    bool cpuPreviewGgxVndf_{true};
    bool cpuPreviewPaused_{false};
    bool cpuPreviewSingleFrameRefresh_{false};
    bool cpuPreviewRestartRequested_{true};

    bool showAbout_{false};
    bool showImGuiDemo_{false};
    bool hierarchyPanelOpen_{true};
    bool inspectorPanelOpen_{true};
    bool assetsPanelOpen_{true};
    bool autoRotate_{false};
    bool showGroundPlane_{true};
    bool showComparisonObject_{false};
    bool vsync_{true};
    bool lastLoadFailed_{false};
    bool prismDemoEnabled_{false};
    bool glassVolumeDemoEnabled_{false};
    bool glassCausticsDemoEnabled_{false};
    bool lightStressDemoEnabled_{false};
    bool instanceStressDemoEnabled_{false};
    bool prismCameraLocked_{true};
    bool prismModelVisible_{true};
    bool benchmarkMode_{false};
    bool prismReelMode_{false};
    bool referenceComparisonMode_{false};
    bool referenceComparisonComplete_{false};
    bool referenceComparisonFailed_{false};
    bool temporalMotionDemoEnabled_{false};
    float cameraHeightDemoStep_{0.0f};
    bool objectMotionDemoEnabled_{false};
    int objectMotionDemoFrame_{0};
    bool sceneFoundationDemoEnabled_{false};
    bool animationDemoEnabled_{false};
    bool animationEnabled_{false};
    bool animationPlaying_{true};
    bool animationTimeFixed_{false};
    float animationTimeSeconds_{0.0f};
    float animationSpeed_{1.0f};
    float animationFrameStep_{0.0f};
    int animationDemoFrame_{0};
    std::size_t animationClipIndex_{0U};
    int localLightTierIndex_{1};
    VolumeGlassPreset volumeGlassPreset_{VolumeGlassPreset::Olive};
    PrismOpticalPreset prismOpticalPreset_{PrismOpticalPreset::CrownGlass};
    PrismDemoParameters prismParameters_{};
    double previousFrameTime_{0.0};
    float frameDeltaTime_{0.0f};
};
