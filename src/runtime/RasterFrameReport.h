#pragma once
#include "asset/InputManifest.h"
#include <filesystem>
#include <string>
#include "runtime/RenderJob.h"
#include "render/Renderer.h"
// Writes the sidecar staging file before the caller publishes the PNG/report pair.
bool writeRasterFrameReport(const std::filesystem::path& path, const RenderJob& job,
    int frame, const RendererSettings& effectiveSettings, const std::string& gpu,
    std::string& error, const capture::InputManifest* manifest = nullptr);
