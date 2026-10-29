#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "module/ParameterRegistry.h"
#include "pathtracer/ProgressiveRenderer.h"

enum class RenderJobFailurePolicy {
    Stop = 0,
    Continue
};

// One C++ module drives a job's scene over its frame range. The persisted form is
// neutral (JSON bool / number / string / triple), so a job stays hand-editable and
// the module's own ParameterRegistry decides what a value means.
struct RenderJobModule {
    std::string id;
    std::uint32_t seed{0U};
    std::vector<ModuleParameterOverride> parameters;
};

struct RenderJob {
    // Schema 2 adds modules; schema 3 adds raster capture; schema 4 opts into
    // sequential ShaderScene feedback. Old jobs
    // keep loading with zero warmup, fixed sampling and temporal accumulation disabled.
    static constexpr int currentSchemaVersion = 4;
    static constexpr int minimumSchemaVersion = 1;

    int schemaVersion{currentSchemaVersion};
    std::filesystem::path sourcePath;
    std::string loadedSourceFingerprint; // Exact authored bytes parsed by the loader.
    std::filesystem::path scenePath;
    std::string renderer{"cpu-path-traced"};
    std::string camera{"scene"};
    pathtracer::RenderSettings renderSettings;
    RenderJobModule module;
    int startFrame{0};
    int endFrame{0};
    int framesPerSecond{24};
    std::vector<pathtracer::RenderOutput> outputs{pathtracer::RenderOutput::Beauty};
    std::vector<pathtracer::RenderFileFormat> outputFormats{
        pathtracer::RenderFileFormat::Png,
        pathtracer::RenderFileFormat::RadianceHdr
    };
    std::filesystem::path outputStemPattern;
    std::filesystem::path simulationCache;
    bool resume{false};
    bool gpuTemporalDenoise{false};
    bool rasterDeterminism{true};
    std::string rasterBackend{"opengl"};
    int rasterWarmupFrames{0};
    bool rasterTemporalAccumulation{false};
    // Explicit schema 4 opt-in: sequential fixed-time feedback, cleared once.
    bool shaderSceneSequence{false};
    int shaderSceneWarmupFrames{0};
    RenderJobFailurePolicy failurePolicy{RenderJobFailurePolicy::Stop};
};

bool loadRenderJob(const std::filesystem::path& path, RenderJob& job, std::string& error);
bool validateRenderJob(const RenderJob& job, std::string& error);
bool validateQueueRenderJob(const RenderJob& job, std::string& error);
bool applyRenderJobOutputOverride(RenderJob& job, const std::filesystem::path& outputStemPattern,
                                  std::string& error);
std::filesystem::path renderJobFrameStem(const RenderJob& job, int frame);
inline std::filesystem::path renderJobFramePath(const RenderJob& job, int frame, const char* suffix) {
    auto path = renderJobFrameStem(job, frame); path += suffix; return path;
}
const char* renderOutputName(pathtracer::RenderOutput output);
const char* renderFileFormatName(pathtracer::RenderFileFormat format);
const char* renderFileFormatExtension(pathtracer::RenderFileFormat format);
