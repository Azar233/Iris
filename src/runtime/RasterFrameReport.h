#pragma once
#include "asset/InputManifest.h"
#include <filesystem>
#include <string>
#include "runtime/RenderJob.h"
#include "render/Renderer.h"
#include "scene/SceneDocument.h"
// Writes the sidecar staging file before the caller publishes the PNG/report pair.
struct ModuleRunReport;
bool writeRasterFrameReport(const std::filesystem::path& path, const RenderJob& job,
    int frame, const RendererSettings& effectiveSettings, const std::string& gpu,
    std::string& error, const capture::InputManifest* manifest = nullptr,
    const iris::ShaderScenePackage* package = nullptr, const SceneDocument* scene = nullptr,
    const ModuleRunReport* moduleReport = nullptr,
    const iris::vulkan::RasterStatistics* vulkan = nullptr);
