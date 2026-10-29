#pragma once
#include "vulkan/VulkanRaster.h"
#include "pathtracer/ProgressiveRenderer.h"
#include "pathtracer/GpuRenderLimits.h"
namespace iris::vulkan {
// Deterministic, unjittered primary-surface guides; independent of Raw Monte Carlo AOVs.
// Pixel centers and motion use top-left pixel coordinates. Motion = previous-current.
// Transmission describes the front surface, never the refracted background.
struct TemporalGuidePixel {
    glm::vec4 motionDepth{}; // motion.xy, current and predicted previous positive view depth
    glm::vec4 normal{}; // world geometric normal; w = flags (hit=1, valid motion=2, transmission=4)
    glm::vec4 previousNormal{}; // predicted previous world normal; w = AS instance ordinal+1
    glm::uvec4 identity{}; // entity id low/high, mesh index, source material index+1
};
struct TemporalGuideFrame {
    unsigned width{},height{};
    int frame{},previousFrame{},framesPerSecond{30};
    bool historyValid{false};
    glm::mat4 currentView{1},previousView{1};
    float currentVerticalFov{},previousVerticalFov{};
    glm::vec2 currentJitter{0},previousJitter{0}; // guides use pixel centers; Raw jitter is unchanged
    double cpuMilliseconds{},gpuMilliseconds{};
    std::uint64_t allocatedBytes{};
    std::vector<TemporalGuidePixel> pixels;
};
class VulkanPathTracer {
public:
    VulkanPathTracer(const std::filesystem::path& shaders,pathtracer::SceneSnapshot scene,
        pathtracer::RenderSettings settings,bool requireValidation,bool manageGlfw=false);
    ~VulkanPathTracer();
    VulkanPathTracer(const VulkanPathTracer&)=delete;
    VulkanPathTracer& operator=(const VulkanPathTracer&)=delete;
    // Returns false when immutable inputs or failed GPU state require a new backend.
    bool restart(const pathtracer::SceneSnapshot&,pathtracer::RenderSettings);
    bool renderPass(const std::atomic<bool>* cancel=nullptr,bool readRaw=true);
    // One in-flight sample; false keeps the last completed preview while polling.
    bool renderPreview(float exposure,bool toneMapping,bool wait=false);
    unsigned completedSamples() const;
    const std::vector<unsigned char>& previewRgba() const;
    const pathtracer::RenderImage& image() const;
    // Actual program used by this immutable geometry layout, for provenance.
    const char* raygenShaderName() const;
    const RasterStatistics& statistics() const;
    // Call for each timeline update. Same frame supports paused camera/rigid edits;
    // only +1 at the same FPS is continuous. Explicit cuts always invalidate.
    void setTemporalFrame(int frame,int framesPerSecond,bool cameraCut=false);
    void invalidateTemporalHistory();
    // Lazy GPU pass, cached until restart/timeline update. Requires a completed sample.
    const TemporalGuideFrame& temporalGuides();
    // Display-only filtering; never changes sampling, Raw/AOVs or their RNG.
    void setTemporalDenoise(bool enabled);
    const pathtracer::RenderImage& accumulatedImage();
    void setSpatialDenoise(bool enabled);
    const pathtracer::RenderImage& spatialImage();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
