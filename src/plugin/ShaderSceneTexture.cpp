#include "plugin/ShaderScene.h"
#include "asset/InputManifest.h"
#include <algorithm>
#include <fstream>
#include <memory>
#include <stdexcept>

// Private PNG decoder: no duplicate public stb symbols or process-wide flip state.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 2048
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4505)
#endif
#include "asset/StbImageCompat.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace iris {
std::size_t shaderSceneTextureBytes(const ShaderSceneTexture& texture) {
    int width = texture.width, height = texture.height, depth = texture.depth;
    std::size_t bytes = 0;
    do {
        bytes += static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
            * static_cast<std::size_t>(depth) * (texture.format == ShaderSceneTextureFormat::LinearR8 ? 1U : 4U);
        if (!texture.mipmaps || (width == 1 && height == 1 && depth == 1)) break;
        width = std::max(1, width / 2); height = std::max(1, height / 2); depth = std::max(1, depth / 2);
    } while (true);
    return bytes;
}
void loadShaderSceneTextureData(ShaderSceneTexture& texture) {
    const auto bytes = static_cast<std::size_t>(texture.width) * texture.height * texture.depth
        * (texture.format == ShaderSceneTextureFormat::LinearR8 ? 1U : 4U);
    if (texture.encoding == ShaderSceneTextureEncoding::HashV1) {
        texture.texels.resize(bytes);
        for (std::size_t i = 0; i < bytes; ++i) {
            auto value = static_cast<std::uint32_t>(i) + texture.seed;
            value ^= value >> 16U; value *= 0x7feb352dU;
            value ^= value >> 15U; value *= 0x846ca68bU; value ^= value >> 16U;
            texture.texels[i] = static_cast<unsigned char>(value & 0xffU);
        }
        texture.fingerprint = capture::fingerprintBytes(std::string(texture.texels.begin(), texture.texels.end()));
        return;
    }
    if (!std::filesystem::is_regular_file(texture.path) || std::filesystem::file_size(texture.path) == 0
        || std::filesystem::file_size(texture.path) > 32U * 1024U * 1024U)
        throw std::runtime_error("Texture file must contain 1..32 MiB: " + texture.path.string());
    const auto input = capture::snapshot(texture.path);
    if (!input.exists || input.bytes == 0 || input.bytes > 32U * 1024U * 1024U)
        throw std::runtime_error("Texture changed beyond file budget during fingerprint: " + texture.path.string());
    if (texture.encoding == ShaderSceneTextureEncoding::Raw && input.bytes != bytes)
        throw std::runtime_error("Raw texture byte count mismatch: " + texture.path.string());
    capture::recordInput(texture.path);
    std::ifstream file(texture.path, std::ios::binary);
    std::vector<unsigned char> encoded(static_cast<std::size_t>(input.bytes));
    if (!file.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size())))
        throw std::runtime_error("Cannot read texture: " + texture.path.string());
    const auto fingerprint = capture::fingerprintBytes(std::string(encoded.begin(), encoded.end()));
    if (fingerprint != input.fingerprint || !capture::same(input, capture::snapshot(texture.path)))
        throw std::runtime_error("Texture changed during read: " + texture.path.string());
    texture.fingerprint = fingerprint; texture.encodedBytes = encoded.size();
    if (texture.encoding == ShaderSceneTextureEncoding::Raw) { texture.texels = std::move(encoded); return; }
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels)
        || width != texture.width || height != texture.height)
        throw std::runtime_error("PNG texture extent mismatch/invalid header: " + texture.path.string());
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 4), stbi_image_free);
    if (!pixels) throw std::runtime_error("Invalid PNG texture: " + texture.path.string());
    texture.texels.assign(pixels.get(), pixels.get() + bytes);
}
bool shaderSceneTextureInputsCurrent(const ShaderScenePackage& package) {
    for (const auto& texture : package.textures) if (!texture.path.empty()) {
        const auto current = capture::snapshot(texture.path);
        if (!current.exists || current.bytes != texture.encodedBytes || current.fingerprint != texture.fingerprint) return false;
    }
    return true;
}
}
