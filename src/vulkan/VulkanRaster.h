#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "render/SceneDrawList.h"
namespace pathtracer { class SceneSnapshot; }
namespace iris::vulkan {
struct RasterStatistics {
    RasterVisibilityStatistics visibility;
    double cpuMilliseconds{0}, gpuMilliseconds{0};
    std::uint64_t allocatedBytes{0};
    std::uint64_t deviceLocalBytes{0}, swapchainPayloadEstimateBytes{0};
    std::uint32_t triangles{0}, draws{0}, validationWarnings{0}, validationErrors{0};
    bool validationEnabled{false}, synchronizationValidation{false};
    bool rayQueryAvailable{false}, rayQueryShadows{false}, shadowDebug{false};
    std::uint32_t blasCount{0}, instanceCount{0}, blasBuilds{0}, tlasBuilds{0}, tlasUpdates{0};
    std::uint64_t accelerationBytes{0};
    double accelerationCpuMilliseconds{0}, accelerationGpuMilliseconds{0};
    // CPU wall intervals; GPU execution overlaps submit/wait and is not additive.
    double geometryPreparationMilliseconds{0}, resourcePreparationMilliseconds{0};
    // Nested CPU subsets; do not add these to the parent pass intervals.
    double assetFingerprintMilliseconds{0}, meshFingerprintMilliseconds{0}, textureFingerprintMilliseconds{0};
    double textureInputMilliseconds{0}, environmentPreparationMilliseconds{0}, shaderInputMilliseconds{0};
    double pipelineCreationMilliseconds{0}, submitWaitMilliseconds{0}, readbackMilliseconds{0};
    double resourceReleaseMilliseconds{0};
    double uploadGpuMilliseconds{0}, drawGpuMilliseconds{0}, readbackGpuMilliseconds{0};
    std::uint64_t hostUploadBytes{0}, readbackBytes{0};
    // PT totals since the latest history restart; displayUploads is per host frame.
    std::uint64_t rawReadbackBytes{0}, previewReadbackBytes{0};
    std::uint32_t previewDispatches{0}, displayUploads{0};
    // Monotonic completion count survives camera restarts; current-camera SPP does not.
    std::uint64_t completedSampleDispatches{0};
    double completedSampleGpuMilliseconds{0};
    double spatialCpuMilliseconds{0},spatialGpuMilliseconds{0},guideGpuMilliseconds{0},guideCpuMilliseconds{0};
    std::uint64_t guideReadbackBytes{0};
    std::uint64_t spatialAllocatedBytes{0},spatialReadbackBytes{0};
    std::uint32_t spatialDispatches{0};
    double temporalCpuMilliseconds{0},temporalGpuMilliseconds{0};
    std::uint64_t temporalAllocatedBytes{0},temporalReadbackBytes{0};
    std::uint32_t temporalDispatches{0},temporalAccepted{0},temporalRejected{0},temporalClamped{0},temporalRepeated{0};
    // CPU file-content storage is separate from Vulkan allocations.
    std::uint64_t fileContentCacheBytes{0}, fileContentReadBytes{0}, fileContentHashBytes{0};
    std::uint32_t pipelineCreations{0}, textureUploads{0}, textureDecodes{0};
    std::string presentMode{"not-presented"};
    std::string device;
    // Legacy PT build counters above describe initialization; these are per call.
    std::uint32_t dispatchBlasBuilds{0}, dispatchTlasBuilds{0}, dispatchTlasUpdates{0};
    std::uint32_t readbackMemoryType{0}, readbackMemoryFlags{0};
    std::uint64_t backendGeneration{0};
    bool backendInitializedThisFrame{false};
};
class VulkanRaster {
public:
    // GLFW lifetime belongs to the host; all calls and destruction stay on its thread.
    explicit VulkanRaster(const std::filesystem::path& shaderDirectory, bool requireValidation);
    ~VulkanRaster();
    VulkanRaster(const VulkanRaster&)=delete;
    VulkanRaster& operator=(const VulkanRaster&)=delete;
    std::vector<unsigned char> render(const pathtracer::SceneSnapshot&, int width, int height,
                                      float exposure, bool toneMapping, bool cullBackFaces = false,
                                      bool rayQueryShadows = false, bool shadowDebug = false, bool frustumCulling = false);
    const RasterStatistics& statistics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
