#include "pathtracer/TextureSampling.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "asset/StbImageCompat.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace pathtracer {
namespace {

float srgbToLinear(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

int wrap(int value, int extent) {
    const int result = value % extent;
    return result < 0 ? result + extent : result;
}

bool finite(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    );
}

struct DecodedImage {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> pixels;
};

std::optional<DecodedImage> decodeAsciiPpm(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 2U || bytes[0] != 'P' || bytes[1] != '3') return std::nullopt;
    try {
        const std::string_view input(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::size_t cursor = 0U;
        const auto nextToken = [&]() -> std::string_view {
            while (cursor < input.size()) {
                if (input[cursor] == '#') {
                    cursor = input.find('\n', cursor);
                    if (cursor == std::string_view::npos) return {};
                } else if (input[cursor] == ' ' || input[cursor] == '\t'
                           || input[cursor] == '\r' || input[cursor] == '\n') {
                    ++cursor;
                } else {
                    break;
                }
            }
            const std::size_t begin = cursor;
            while (cursor < input.size() && input[cursor] != ' ' && input[cursor] != '\t'
                   && input[cursor] != '\r' && input[cursor] != '\n' && input[cursor] != '#') {
                ++cursor;
            }
            return input.substr(begin, cursor - begin);
        };
        const auto nextInteger = [&]() -> int {
            const std::string_view token = nextToken();
            if (token.empty()) throw std::runtime_error("truncated PPM");
            return std::stoi(std::string(token));
        };

        if (nextToken() != "P3") return std::nullopt;
        DecodedImage image;
        image.width = nextInteger();
        image.height = nextInteger();
        const int maximum = nextInteger();
        if (image.width <= 0 || image.height <= 0 || maximum <= 0) return std::nullopt;
        const std::size_t pixelCount = static_cast<std::size_t>(image.width) * image.height;
        if (pixelCount > std::numeric_limits<std::size_t>::max() / 4U) return std::nullopt;
        image.pixels.resize(pixelCount * 4U);
        for (std::size_t pixel = 0U; pixel < pixelCount; ++pixel) {
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
                const int value = nextInteger();
                image.pixels[pixel * 4U + channel] = static_cast<std::uint8_t>(
                    static_cast<long long>(std::clamp(value, 0, maximum)) * 255LL / maximum
                );
            }
            image.pixels[pixel * 4U + 3U] = 255U;
        }
        return image;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace

CpuTexture::CpuTexture(const TextureData& source) {
    const stbi_uc* pixels = nullptr;
    stbi_uc* decoded = nullptr;
    int width = 0;
    int height = 0;
    int components = 0;
    std::optional<DecodedImage> ppm;

    if (!source.rgbaPixels.empty()
        && source.width > 0U
        && source.height > 0U
        && source.width <= static_cast<std::uint32_t>(std::numeric_limits<int>::max())
        && source.height <= static_cast<std::uint32_t>(std::numeric_limits<int>::max())
        && source.rgbaPixels.size()
            >= static_cast<std::size_t>(source.width) * source.height * 4U) {
        width = static_cast<int>(source.width);
        height = static_cast<int>(source.height);
        pixels = source.rgbaPixels.data();
    } else {
        std::vector<std::uint8_t> fileBytes;
        const std::vector<std::uint8_t>* encoded = &source.encodedData;
        if (encoded->empty() && !source.sourcePath.empty()) {
            fileBytes = readBytes(source.sourcePath);
            encoded = &fileBytes;
        }
        if (!encoded->empty()) {
            ppm = decodeAsciiPpm(*encoded);
            if (ppm) {
                width = ppm->width;
                height = ppm->height;
                pixels = ppm->pixels.data();
            } else if (encoded->size()
                       <= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                decoded = stbi_load_from_memory(
                    encoded->data(),
                    static_cast<int>(encoded->size()),
                    &width,
                    &height,
                    &components,
                    STBI_rgb_alpha
                );
                pixels = decoded;
            }
        }
    }

    if (pixels == nullptr || width <= 0 || height <= 0) {
        stbi_image_free(decoded);
        return;
    }

    width_ = static_cast<std::uint32_t>(width);
    height_ = static_cast<std::uint32_t>(height);
    texels_.resize(static_cast<std::size_t>(width_) * height_);
    for (std::uint32_t y = 0U; y < height_; ++y) {
        // Texture2D flips uploads vertically; mirror that storage convention so
        // UVs produce identical base-level samples on CPU and GPU.
        const std::uint32_t sourceY = height_ - 1U - y;
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const std::size_t sourceOffset = (static_cast<std::size_t>(sourceY) * width_ + x) * 4U;
            glm::vec4 texel(
                pixels[sourceOffset] / 255.0f,
                pixels[sourceOffset + 1U] / 255.0f,
                pixels[sourceOffset + 2U] / 255.0f,
                pixels[sourceOffset + 3U] / 255.0f
            );
            if (source.srgb) {
                texel.r = srgbToLinear(texel.r);
                texel.g = srgbToLinear(texel.g);
                texel.b = srgbToLinear(texel.b);
            }
            texels_[static_cast<std::size_t>(y) * width_ + x] = texel;
        }
    }
    stbi_image_free(decoded);
}

glm::vec4 CpuTexture::sampleRepeatBilinear(glm::vec2 texCoord) const {
    if (!valid() || !finite(texCoord)) return glm::vec4(1.0f);

    const float x = texCoord.x * static_cast<float>(width_) - 0.5f;
    const float y = texCoord.y * static_cast<float>(height_) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - std::floor(x);
    const float ty = y - std::floor(y);
    const int width = static_cast<int>(width_);
    const int height = static_cast<int>(height_);
    const auto fetch = [&](int texelX, int texelY) {
        return texels_[static_cast<std::size_t>(wrap(texelY, height)) * width_
            + static_cast<std::size_t>(wrap(texelX, width))];
    };
    const glm::vec4 lower = glm::mix(fetch(x0, y0), fetch(x0 + 1, y0), tx);
    const glm::vec4 upper = glm::mix(fetch(x0, y0 + 1), fetch(x0 + 1, y0 + 1), tx);
    return glm::mix(lower, upper, ty);
}

SceneTextures::SceneTextures(const SceneSnapshot& snapshot) {
    assets_.reserve(snapshot.assets().size());
    for (const SceneSnapshotAsset& asset : snapshot.assets()) {
        std::vector<CpuTexture> textures;
        if (asset.model != nullptr) {
            textures.reserve(asset.model->textures.size());
            for (const TextureData& source : asset.model->textures) textures.emplace_back(source);
        }
        assets_.push_back(std::move(textures));
    }
}

const CpuTexture* SceneTextures::texture(
    std::uint32_t assetIndex,
    std::int32_t textureIndex
) const {
    if (assetIndex >= assets_.size() || textureIndex < 0) return nullptr;
    const auto& textures = assets_[assetIndex];
    const auto index = static_cast<std::size_t>(textureIndex);
    if (index >= textures.size() || !textures[index].valid()) return nullptr;
    return &textures[index];
}

EvaluatedPbrMaterial SceneTextures::evaluate(
    const SurfaceInteraction& hit,
    const MaterialData& material,
    const glm::vec3& linearTint
) const {
    glm::vec4 baseColor = material.baseColorFactor;
    if (const CpuTexture* base = texture(hit.assetIndex, material.baseColorTextureIndex)) {
        baseColor *= base->sampleRepeatBilinear(hit.texCoord);
    }

    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;
    if (const CpuTexture* packed = texture(hit.assetIndex, material.metallicRoughnessTextureIndex)) {
        const glm::vec4 sample = packed->sampleRepeatBilinear(hit.texCoord);
        roughness *= sample.g;
        metallic *= sample.b;
    }
    float thickness = material.thicknessFactor;
    if (const CpuTexture* packed = texture(hit.assetIndex, material.thicknessTextureIndex)) {
        thickness *= packed->sampleRepeatBilinear(hit.texCoord).g;
    }

    glm::vec3 normal = hit.shadingNormal;
    if (const CpuTexture* normalMap = texture(hit.assetIndex, material.normalTextureIndex)) {
        glm::vec3 tangent(hit.tangent);
        tangent -= normal * glm::dot(normal, tangent);
        const float tangentLengthSquared = glm::dot(tangent, tangent);
        if (std::abs(hit.tangent.w) > 0.5f
            && std::isfinite(tangentLengthSquared)
            && tangentLengthSquared > 1.0e-12f) {
            tangent *= 1.0f / std::sqrt(tangentLengthSquared);
            const float handedness = hit.tangent.w < 0.0f ? -1.0f : 1.0f;
            const glm::vec3 bitangent = glm::cross(normal, tangent) * handedness;
            const glm::vec3 tangentNormal = glm::vec3(
                normalMap->sampleRepeatBilinear(hit.texCoord)
            ) * 2.0f - 1.0f;
            const glm::vec3 mapped = tangent * tangentNormal.x
                + bitangent * tangentNormal.y
                + normal * tangentNormal.z;
            const float mappedLengthSquared = glm::dot(mapped, mapped);
            if (std::isfinite(mappedLengthSquared) && mappedLengthSquared > 1.0e-12f) {
                const glm::vec3 candidate = mapped * (1.0f / std::sqrt(mappedLengthSquared));
                if (glm::dot(candidate, hit.geometricNormal) > 0.0f) normal = candidate;
            }
        }
    }

    return {
        {
            glm::clamp(glm::vec3(baseColor) * linearTint, glm::vec3(0.0f), glm::vec3(1.0f)),
            std::clamp(metallic, 0.0f, 1.0f),
            std::clamp(roughness, 0.0f, 1.0f)
        },
        normal,
        std::clamp(baseColor.a, 0.0f, 1.0f),
        std::clamp(material.transmissionFactor, 0.0f, 1.0f),
        std::max(material.indexOfRefraction, 1.0f),
        std::max(thickness, 0.0f),
        glm::clamp(material.attenuationColor, glm::vec3(0.0f), glm::vec3(1.0f)),
        material.attenuationDistance > 0.0f
            ? material.attenuationDistance
            : std::numeric_limits<float>::infinity()
    };
}

} // namespace pathtracer
