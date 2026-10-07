#include "pathtracer/EnvironmentSampling.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#if defined(__GNUC__)
// stb_image with STB_IMAGE_STATIC exposes its whole optional API as unused static
// functions; this keeps the project's own -Wall -Wextra output meaningful.
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
#include <tinyexr.h>

namespace pathtracer {
namespace {

constexpr double pi = 3.14159265358979323846;

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool validImage(std::uint32_t width, std::uint32_t height, const std::vector<glm::vec3>& pixels) {
    if (width == 0U || height == 0U
        || pixels.size() != static_cast<std::size_t>(width) * height) {
        return false;
    }
    bool positive = false;
    for (const glm::vec3& pixel : pixels) {
        if (!finite(pixel) || glm::any(glm::lessThan(pixel, glm::vec3(0.0f)))) return false;
        positive = positive || glm::any(glm::greaterThan(pixel, glm::vec3(0.0f)));
    }
    return positive;
}

bool loadEnvironment(
    const std::filesystem::path& path,
    std::uint32_t& width,
    std::uint32_t& height,
    std::vector<glm::vec3>& pixels
) {
    if (path.empty()) return false;
    int loadedWidth = 0;
    int loadedHeight = 0;
    if (path.extension() == ".exr") {
        float* rgba = nullptr;
        const char* error = nullptr;
        const int result = LoadEXR(
            &rgba, &loadedWidth, &loadedHeight, path.string().c_str(), &error
        );
        if (result != TINYEXR_SUCCESS || rgba == nullptr) {
            if (error != nullptr) FreeEXRErrorMessage(error);
            return false;
        }
        if (loadedWidth <= 0 || loadedHeight <= 0) {
            std::free(rgba);
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(loadedWidth) * loadedHeight;
        pixels.resize(count);
        for (std::size_t index = 0U; index < count; ++index) {
            pixels[index] = {rgba[index * 4U], rgba[index * 4U + 1U], rgba[index * 4U + 2U]};
        }
        std::free(rgba);
    } else {
        int components = 0;
        float* rgb = stbi_loadf(path.string().c_str(), &loadedWidth, &loadedHeight, &components, 3);
        if (rgb == nullptr) return false;
        if (loadedWidth <= 0 || loadedHeight <= 0) {
            stbi_image_free(rgb);
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(loadedWidth) * loadedHeight;
        pixels.resize(count);
        for (std::size_t index = 0U; index < count; ++index) {
            pixels[index] = {rgb[index * 3U], rgb[index * 3U + 1U], rgb[index * 3U + 2U]};
        }
        stbi_image_free(rgb);
    }
    width = static_cast<std::uint32_t>(loadedWidth);
    height = static_cast<std::uint32_t>(loadedHeight);
    return validImage(width, height, pixels);
}

int wrap(int value, int extent) {
    const int result = value % extent;
    return result < 0 ? result + extent : result;
}

float luminance(const glm::vec3& radiance) {
    return glm::dot(radiance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

} // namespace

EnvironmentLight::EnvironmentLight(const SnapshotEnvironment& environment)
    : backgroundColor_(glm::max(environment.backgroundColor, glm::vec3(0.0f))),
      intensity_(std::max(environment.intensity, 0.0f)),
      width_(environment.width),
      height_(environment.height),
      pixels_(environment.radiancePixels) {
    if (!validImage(width_, height_, pixels_)) {
        width_ = height_ = 0U;
        pixels_.clear();
        loadEnvironment(environment.sourcePath, width_, height_, pixels_);
    }
    if (!validImage(width_, height_, pixels_) || intensity_ <= 0.0f) {
        width_ = height_ = 0U;
        pixels_.clear();
        return;
    }

    weights_.resize(pixels_.size());
    cdf_.resize(pixels_.size());
    for (std::uint32_t y = 0U; y < height_; ++y) {
        const double theta = pi * (static_cast<double>(y) + 0.5) / static_cast<double>(height_);
        const double sinTheta = std::sin(theta);
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
            const double weight = std::max(static_cast<double>(luminance(pixels_[index])), 0.0)
                * sinTheta;
            weights_[index] = weight;
            totalWeight_ += weight;
            cdf_[index] = totalWeight_;
        }
    }
    if (!(totalWeight_ > 0.0) || !std::isfinite(totalWeight_)) {
        weights_.clear();
        cdf_.clear();
        totalWeight_ = 0.0;
    }
}

glm::vec3 EnvironmentLight::texel(int x, int y) const {
    x = wrap(x, static_cast<int>(width_));
    y = std::clamp(y, 0, static_cast<int>(height_) - 1);
    return pixels_[static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)];
}

glm::vec3 EnvironmentLight::radiance(const glm::vec3& direction) const {
    const float lengthSquared = glm::dot(direction, direction);
    if (!finite(direction) || lengthSquared <= 1.0e-12f) return glm::vec3(0.0f);
    if (pixels_.empty()) return backgroundColor_ * intensity_;

    const glm::vec3 unit = direction * (1.0f / std::sqrt(lengthSquared));
    const float u = static_cast<float>(std::atan2(unit.z, unit.x) / (2.0 * pi) + 0.5);
    const float v = static_cast<float>(std::acos(std::clamp(unit.y, -1.0f, 1.0f)) / pi);
    const float x = u * static_cast<float>(width_) - 0.5f;
    const float y = v * static_cast<float>(height_) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - std::floor(x);
    const float ty = y - std::floor(y);
    const glm::vec3 top = glm::mix(texel(x0, y0), texel(x0 + 1, y0), tx);
    const glm::vec3 bottom = glm::mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), tx);
    return glm::max(glm::mix(top, bottom, ty), glm::vec3(0.0f)) * intensity_;
}

EnvironmentSample EnvironmentLight::sample(const glm::vec2& random) const {
    EnvironmentSample result;
    if (!importanceSampled() || !std::isfinite(random.x) || !std::isfinite(random.y)) return result;
    const double target = std::clamp(static_cast<double>(random.x), 0.0,
                                     std::nextafter(1.0, 0.0)) * totalWeight_;
    const auto iterator = std::upper_bound(cdf_.begin(), cdf_.end(), target);
    const std::size_t index = std::min<std::size_t>(
        static_cast<std::size_t>(iterator - cdf_.begin()), cdf_.size() - 1U
    );
    const double previous = index == 0U ? 0.0 : cdf_[index - 1U];
    const double interval = std::max(cdf_[index] - previous, std::numeric_limits<double>::min());
    const double texelU = std::clamp((target - previous) / interval, 0.0,
                                     std::nextafter(1.0, 0.0));
    const double texelV = std::clamp(static_cast<double>(random.y), 1.0e-7, 1.0 - 1.0e-7);
    const std::uint32_t x = static_cast<std::uint32_t>(index % width_);
    const std::uint32_t y = static_cast<std::uint32_t>(index / width_);
    const double u = (static_cast<double>(x) + texelU) / static_cast<double>(width_);
    const double v = (static_cast<double>(y) + texelV) / static_cast<double>(height_);
    const double phi = 2.0 * pi * (u - 0.5);
    const double theta = pi * v;
    const double sinTheta = std::sin(theta);
    result.direction = glm::normalize(glm::vec3(
        static_cast<float>(std::cos(phi) * sinTheta),
        static_cast<float>(std::cos(theta)),
        static_cast<float>(std::sin(phi) * sinTheta)
    ));
    result.radiance = radiance(result.direction);
    result.pdf = static_cast<float>(
        (weights_[index] / totalWeight_) * static_cast<double>(width_) * height_
        / (2.0 * pi * pi * sinTheta)
    );
    result.valid = result.pdf > 0.0f && std::isfinite(result.pdf)
        && glm::any(glm::greaterThan(result.radiance, glm::vec3(0.0f)));
    return result;
}

float EnvironmentLight::pdf(const glm::vec3& direction) const {
    const float lengthSquared = glm::dot(direction, direction);
    if (!importanceSampled() || !finite(direction) || lengthSquared <= 1.0e-12f) return 0.0f;
    const glm::vec3 unit = direction * (1.0f / std::sqrt(lengthSquared));
    const double theta = std::acos(std::clamp(static_cast<double>(unit.y), -1.0, 1.0));
    const double sinTheta = std::sin(theta);
    if (sinTheta <= 1.0e-12) return 0.0f;
    const double u = std::atan2(static_cast<double>(unit.z), static_cast<double>(unit.x))
        / (2.0 * pi) + 0.5;
    const double v = theta / pi;
    const std::uint32_t x = std::min(
        static_cast<std::uint32_t>(u * width_), width_ - 1U
    );
    const std::uint32_t y = std::min(
        static_cast<std::uint32_t>(v * height_), height_ - 1U
    );
    const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
    return static_cast<float>(
        (weights_[index] / totalWeight_) * static_cast<double>(width_) * height_
        / (2.0 * pi * pi * sinTheta)
    );
}

} // namespace pathtracer
