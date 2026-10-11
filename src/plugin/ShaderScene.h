#pragma once
#include <filesystem>
#include <string>
#include "module/ParameterRegistry.h"

namespace iris {
// v1 is deliberately a single, full-resolution display output with GLSL main().
// This CPU descriptor is shared by the editor, runtime and preflight tests.
struct ShaderScenePackage {
    std::filesystem::path manifest;
    std::filesystem::path root;
    std::filesystem::path fragment;
    std::string id;
    std::string author;
    std::string license;
    ParameterRegistry parameters;
};
bool loadShaderScenePackage(const std::filesystem::path& path, ShaderScenePackage& package,
    std::string& error);
}
