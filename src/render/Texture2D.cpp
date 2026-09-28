#include "asset/InputManifest.h"
#include "render/Texture2D.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <glad/gl.h>

#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Texture file does not exist or cannot be opened: " + path.string());
    }
    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    };
}

std::string textureLabel(const TextureData& source) {
    if (!source.sourcePath.empty()) {
        return source.sourcePath.string();
    }
    return source.name.empty() ? source.cacheKey : source.name;
}

struct DecodedImage {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> pixels;
};

std::optional<DecodedImage> decodeAsciiPpm(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 2U || bytes[0] != 'P' || bytes[1] != '3') {
        return std::nullopt;
    }
    const std::string_view input(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::size_t cursor = 0;
    auto nextToken = [&]() -> std::string_view {
        while (cursor < input.size()) {
            if (input[cursor] == '#') {
                cursor = input.find('\n', cursor);
                if (cursor == std::string_view::npos) {
                    return {};
                }
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
    auto nextInteger = [&]() -> int {
        const std::string_view token = nextToken();
        if (token.empty()) {
            throw std::runtime_error("ASCII PPM image is truncated");
        }
        return std::stoi(std::string(token));
    };

    if (nextToken() != "P3") {
        return std::nullopt;
    }
    DecodedImage image;
    image.width = nextInteger();
    image.height = nextInteger();
    const int maximum = nextInteger();
    if (image.width <= 0 || image.height <= 0 || maximum <= 0) {
        throw std::runtime_error("ASCII PPM image has invalid dimensions or range");
    }
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4U);
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(image.width) * image.height; ++pixel) {
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const int value = nextInteger();
            image.pixels[pixel * 4U + channel] = static_cast<std::uint8_t>(
                std::clamp(value, 0, maximum) * 255 / maximum
            );
        }
        image.pixels[pixel * 4U + 3U] = 255U;
    }
    return image;
}

} // namespace

Texture2D::Texture2D(
    const std::uint8_t* rgbaPixels,
    int width,
    int height,
    bool flipVertically,
    bool srgb
)
    : width_(width), height_(height) {
    if (rgbaPixels == nullptr || width <= 0 || height <= 0) {
        throw std::runtime_error("Cannot create a texture from empty pixel data");
    }

    const std::uint8_t* uploadPixels = rgbaPixels;
    std::vector<std::uint8_t> flippedPixels;
    if (flipVertically) {
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4U;
        flippedPixels.resize(rowBytes * static_cast<std::size_t>(height));
        for (int row = 0; row < height; ++row) {
            const auto* sourceRow = rgbaPixels + static_cast<std::size_t>(height - row - 1) * rowBytes;
            std::copy(sourceRow, sourceRow + rowBytes, flippedPixels.data() + static_cast<std::size_t>(row) * rowBytes);
        }
        uploadPixels = flippedPixels.data();
    }

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
        width_,
        height_,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        uploadPixels
    );
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

Texture2D::~Texture2D() {
    if (id_ != 0U) {
        glDeleteTextures(1, &id_);
    }
}

std::shared_ptr<Texture2D> Texture2D::fromSource(const TextureData& source) {
    if (!source.rgbaPixels.empty()) {
        const std::size_t expectedSize = static_cast<std::size_t>(source.width) * source.height * 4U;
        if (source.width == 0U || source.height == 0U || source.rgbaPixels.size() < expectedSize) {
            throw std::runtime_error("Embedded RGBA texture has invalid dimensions: " + textureLabel(source));
        }
        return std::shared_ptr<Texture2D>(new Texture2D(
            source.rgbaPixels.data(),
            static_cast<int>(source.width),
            static_cast<int>(source.height),
            true,
            source.srgb
        ));
    }

    std::vector<std::uint8_t> bytes = source.encodedData;
    if (bytes.empty() && !source.sourcePath.empty()) {
        capture::recordInput(source.sourcePath);
        bytes = readBinaryFile(source.sourcePath);
    }
    if (bytes.empty()) {
        throw std::runtime_error("Texture source contains no image data: " + textureLabel(source));
    }

    if (auto ppm = decodeAsciiPpm(bytes)) {
        return std::shared_ptr<Texture2D>(new Texture2D(
            ppm->pixels.data(),
            ppm->width,
            ppm->height,
            true,
            source.srgb
        ));
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(1);
    stbi_uc* pixels = stbi_load_from_memory(
        bytes.data(),
        static_cast<int>(bytes.size()),
        &width,
        &height,
        &channels,
        STBI_rgb_alpha
    );
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(
            "Could not decode texture " + textureLabel(source)
            + (reason == nullptr ? std::string{} : ": " + std::string(reason))
        );
    }
    auto texture = std::shared_ptr<Texture2D>(new Texture2D(pixels, width, height, false, source.srgb));
    stbi_image_free(pixels);
    return texture;
}

std::shared_ptr<Texture2D> Texture2D::solidColor(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha,
    bool srgb
) {
    const std::uint8_t pixel[] = {red, green, blue, alpha};
    return std::shared_ptr<Texture2D>(new Texture2D(pixel, 1, 1, false, srgb));
}

std::shared_ptr<Texture2D> Texture2D::missingTexture(bool srgb) {
    constexpr std::uint8_t pixels[] = {
        255, 0, 255, 255,  24, 24, 24, 255,
        24, 24, 24, 255,  255, 0, 255, 255
    };
    return std::shared_ptr<Texture2D>(new Texture2D(pixels, 2, 2, false, srgb));
}

void Texture2D::bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, id_);
}

std::size_t Texture2D::estimatedBytes() const {
    return static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U * 4U / 3U;
}

TextureCache::TextureCache()
    : whiteTexture_(Texture2D::solidColor(255, 255, 255, 255, true)),
      linearWhiteTexture_(Texture2D::solidColor(255, 255, 255, 255, false)),
      missingTexture_(Texture2D::missingTexture(true)),
      flatNormalTexture_(Texture2D::solidColor(128, 128, 255, 255, false)) {
}

TextureLoadResult TextureCache::load(const TextureData& source) {
    const std::string key = source.cacheKey.empty() ? textureLabel(source) : source.cacheKey;
    const auto found = textures_.find(key);
    if (found != textures_.end()) {
        if (auto texture = found->second.lock()) {
            return {texture, fallbackKeys_.find(key) != fallbackKeys_.end(), {}};
        }
    }

    try {
        auto texture = Texture2D::fromSource(source);
        textures_[key] = texture;
        fallbackKeys_.erase(key);
        return {std::move(texture), false, {}};
    } catch (const std::exception& error) {
        const auto& fallback = source.srgb ? missingTexture_ : flatNormalTexture_;
        textures_[key] = fallback;
        fallbackKeys_.insert(key);
        return {
            fallback,
            true,
            std::string(error.what()) + (source.srgb
                ? "; using the missing-texture checkerboard."
                : "; disabling the data texture and using a neutral fallback.")
        };
    }
}

std::size_t TextureCache::cachedTextureCount() const {
    std::size_t count = 0;
    for (const auto& entry : textures_) {
        if (!entry.second.expired()) {
            ++count;
        }
    }
    return count;
}
