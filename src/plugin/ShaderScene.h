#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include "module/ParameterRegistry.h"
#include "plugin/ShaderSceneHistory.h"
struct RendererSettings;

namespace iris {
inline constexpr std::size_t shaderSceneMaximumPasses = 4;
inline constexpr std::size_t shaderSceneMaximumChannels = 4;
enum class ShaderSceneFormat { DisplayRgba8, LinearRgba16f };
enum class ShaderSceneTextureFormat { LinearR8, LinearRgba8, SrgbRgba8 };
enum class ShaderSceneTextureEncoding { Raw, Png, HashV1 };
inline constexpr std::size_t shaderSceneMaximumTextureBytes = 64U * 1024U * 1024U;
struct ShaderSceneTexture {
    std::string id;
    std::filesystem::path path;
    ShaderSceneTextureFormat format{ShaderSceneTextureFormat::LinearR8};
    ShaderSceneTextureEncoding encoding{ShaderSceneTextureEncoding::Raw};
    bool volume{false}, mipmaps{false};
    int width{0}, height{0}, depth{1};
    std::uint32_t seed{0};
    std::vector<unsigned char> texels;
    std::string fingerprint;
    std::size_t encodedBytes{0};
};
std::size_t shaderSceneTextureBytes(const ShaderSceneTexture& texture);
void loadShaderSceneTextureData(ShaderSceneTexture& texture);
struct ShaderSceneChannel {
    int slot{0};
    std::string source;
    std::size_t sourcePass{0}; // Resolved by CPU validation.
    bool previousFrame{false};
    bool linearFilter{false};
    bool repeatWrap{false};
    bool externalTexture{false};
    std::size_t sourceTexture{0};
    bool mipmaps{false};
};
struct ShaderScenePass {
    std::string id;
    std::filesystem::path fragment;
    ShaderSceneFormat format{ShaderSceneFormat::DisplayRgba8};
    int sizeDivisor{1};
    std::vector<ShaderSceneChannel> channels;
};
// v1 normalizes to one Image; v2 declares a graph; v3 adds immutable texture inputs.
struct ShaderScenePackage {
    std::filesystem::path manifest;
    std::filesystem::path root;
    std::string id;
    std::string author;
    std::string license;
    ParameterRegistry parameters;
    int version{1};
    bool mainImageEntry{false}; // v2/v3; applies to every Pass.
    bool modelMaterial{false}; // .shadermaterial v1, opaque Forward surface, linear output.
    std::vector<ShaderScenePass> passes;
    std::vector<std::size_t> executionOrder;
    std::vector<ShaderSceneTexture> textures; // v3 only; CPU staging released after upload.
    std::string parameterSource; // Optional CPU plugin schema, v3 only.
};
struct ShaderSceneRuntimeStatus {
    int width{0}, height{0}, frame{0};
    float time{0}, timeDelta{0};
    bool rendered{false}, historyValid{false}, resetPending{false};
};
bool setShaderSceneParameter(const ShaderScenePackage& package, RendererSettings& settings,
    const ModuleParameterOverride& parameter, bool& changed, std::string& error);
std::string shaderSceneResourceFor(const RendererSettings& settings);
std::vector<ModuleParameterValue> shaderSceneParameterValues(const ShaderScenePackage& package, const RendererSettings& settings);
bool shaderScenePassNeedsHistory(const ShaderScenePackage& package, std::size_t index);
bool shaderSceneTextureInputsCurrent(const ShaderScenePackage& package);
bool loadShaderScenePackage(const std::filesystem::path& path, ShaderScenePackage& package,
    std::string& error);
}
