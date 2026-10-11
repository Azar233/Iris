#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "module/ParameterRegistry.h"

namespace iris {
inline constexpr std::size_t shaderSceneMaximumPasses = 4;
inline constexpr std::size_t shaderSceneMaximumChannels = 4;
inline constexpr float shaderSceneMaximumTimeStep = 0.25f;
enum class ShaderSceneFormat { DisplayRgba8, LinearRgba16f };
struct ShaderSceneChannel {
    int slot{0};
    std::string source;
    std::size_t sourcePass{0}; // Resolved by CPU validation.
    bool previousFrame{false};
    bool linearFilter{false};
    bool repeatWrap{false};
};
struct ShaderScenePass {
    std::string id;
    std::filesystem::path fragment;
    ShaderSceneFormat format{ShaderSceneFormat::DisplayRgba8};
    int sizeDivisor{1};
    std::vector<ShaderSceneChannel> channels;
};
// v1 normalizes to one Image; v2 declares a bounded ordered graph.
struct ShaderScenePackage {
    std::filesystem::path manifest;
    std::filesystem::path root;
    std::string id;
    std::string author;
    std::string license;
    ParameterRegistry parameters;
    int version{1};
    bool mainImageEntry{false}; // v2 only; applies to every Pass.
    std::vector<ShaderScenePass> passes;
    std::vector<std::size_t> executionOrder;
};
bool loadShaderScenePackage(const std::filesystem::path& path, ShaderScenePackage& package,
    std::string& error);
}
