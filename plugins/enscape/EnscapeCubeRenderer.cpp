#include "EnscapeCubeRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <glad/gl.h>

#include "render/RenderTarget.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "render/Shader.h"

namespace {
std::uint32_t hash(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

float randomAt(int x, int y, int period) {
    const auto xx = static_cast<std::uint32_t>((x % period + period) % period);
    const auto yy = static_cast<std::uint32_t>((y % period + period) % period);
    return static_cast<float>(hash(xx + yy * 131U + 19U) & 0xffffU) / 65535.0f;
}

float periodicNoise(float x, float y, int period) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(xi);
    const float fy = y - static_cast<float>(yi);
    const float u = fx * fx * (3.0f - 2.0f * fx);
    const float v = fy * fy * (3.0f - 2.0f * fy);
    const float a = randomAt(xi, yi, period);
    const float b = randomAt(xi + 1, yi, period);
    const float c = randomAt(xi, yi + 1, period);
    const float d = randomAt(xi + 1, yi + 1, period);
    return (a + (b - a) * u) * (1.0f - v) + (c + (d - c) * u) * v;
}
} // namespace

EnscapeCubeRenderer::EnscapeCubeRenderer(const std::filesystem::path& shaderDirectory) {
    try {
    const auto directory = shaderDirectory / "third_party" / "enscape_cube";
    const auto vertex = shaderDirectory / "fullscreen.vert";
    shaders_[0] = std::make_unique<Shader>(vertex, directory / "pass_a.frag");
    shaders_[1] = std::make_unique<Shader>(vertex, directory / "pass_b.frag");
    shaders_[2] = std::make_unique<Shader>(vertex, directory / "pass_c.frag");
    shaders_[3] = std::make_unique<Shader>(vertex, directory / "pass_image.frag");
    makeNoiseTextures();
    glGenQueries(static_cast<GLsizei>(timingQueries_.size()), timingQueries_.data());
    } catch (...) { releaseResources(); throw; }
}

void EnscapeCubeRenderer::renderFrame(const iris::RenderPluginFrame& frame) {
    iris::validatePluginBindings(iris::enscapeContract(), frame.textures, frame.width, frame.height);
    render(frame.target, frame.camera, frame.settings, frame.width, frame.height,
        frame.timeSeconds, frame.fullscreenVertexArray);
}
void EnscapeCubeRenderer::prepareResources(const RendererSettings&, int width, int height) {
    resize(width, height);
    for (const auto& shader : shaders_)
        if (!shader->sourcesCurrent()) throw std::runtime_error("Enscape Shader inputs changed during preparation");
}

iris::RenderPluginFrameInfo EnscapeCubeRenderer::frameInfo() const {
    return {{"Enscape Cube: ocean and clouds", "Enscape Cube: bloom and tone map",
        "Enscape Cube: TAA", "Enscape Cube: final image"},
        4U, gpuTimeValid_, gpuTimeUpdated_, gpuTimeMilliseconds_};
}

EnscapeCubeRenderer::~EnscapeCubeRenderer() { releaseResources(); }

void EnscapeCubeRenderer::releaseResources() {
    glDeleteQueries(static_cast<GLsizei>(timingQueries_.size()), timingQueries_.data());
    glDeleteFramebuffers(static_cast<GLsizei>(framebuffers_.size()), framebuffers_.data());
    glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
    if (weatherTexture_ != 0U) glDeleteTextures(1, &weatherTexture_);
    if (volumeTexture_ != 0U) glDeleteTextures(1, &volumeTexture_);
}

void EnscapeCubeRenderer::makeNoiseTextures() {
    constexpr int weatherSize = 256;
    std::vector<std::uint8_t> weather(weatherSize * weatherSize);
    for (int y = 0; y < weatherSize; ++y) {
        for (int x = 0; x < weatherSize; ++x) {
            const float nx = static_cast<float>(x) / weatherSize;
            const float ny = static_cast<float>(y) / weatherSize;
            const float value = 0.54f * periodicNoise(nx * 4.0f, ny * 4.0f, 4)
                + 0.30f * periodicNoise(nx * 16.0f, ny * 16.0f, 16)
                + 0.16f * periodicNoise(nx * 64.0f, ny * 64.0f, 64);
            weather[static_cast<std::size_t>(y) * weatherSize + x] =
                static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
        }
    }
    glGenTextures(1, &weatherTexture_);
    glBindTexture(GL_TEXTURE_2D, weatherTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, weatherSize, weatherSize, 0,
        GL_RED, GL_UNSIGNED_BYTE, weather.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);

    constexpr int volumeSize = 32;
    std::vector<std::uint8_t> volume(volumeSize * volumeSize * volumeSize);
    for (std::size_t index = 0; index < volume.size(); ++index) {
        volume[index] = static_cast<std::uint8_t>(hash(static_cast<std::uint32_t>(index)) & 0xffU);
    }
    glGenTextures(1, &volumeTexture_);
    glBindTexture(GL_TEXTURE_3D, volumeTexture_);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, volumeSize, volumeSize, volumeSize, 0,
        GL_RED, GL_UNSIGNED_BYTE, volume.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glBindTexture(GL_TEXTURE_3D, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void EnscapeCubeRenderer::resize(int width, int height) {
    if (width_ == width && height_ == height) return;
    glDeleteFramebuffers(static_cast<GLsizei>(framebuffers_.size()), framebuffers_.data());
    glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
    framebuffers_.fill(0U);
    textures_.fill(0U);
    glGenTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
    glGenFramebuffers(static_cast<GLsizei>(framebuffers_.size()), framebuffers_.data());
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        glBindTexture(GL_TEXTURE_2D, textures_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures_[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Enscape Cube pass framebuffer is incomplete");
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    width_ = width;
    height_ = height;
    historyValid_ = false;
    historyIndex_ = 0U;
}

void EnscapeCubeRenderer::draw(Shader& shader, unsigned int framebuffer,
    int width, int height, float timeSeconds, unsigned int fullscreenVertexArray,
    const Camera& camera, const EnscapeCubeSettings& parameters) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    shader.use();
    shader.setVec3("iResolution", {static_cast<float>(width), static_cast<float>(height), 1.0f});
    shader.setFloat("iTime", timeSeconds);
    shader.setVec3("uCameraPosition", camera.position());
    shader.setVec3("uCameraForward", camera.forwardDirection());
    shader.setVec3("uCameraRight", camera.rightDirection());
    shader.setVec3("uCameraUp", camera.upDirection());
    shader.setFloat("uCameraTanHalfFov",
        std::tan(camera.fieldOfView() * 0.008726646259971648f));
    shader.setFloat("uCameraNear", camera.nearPlane());
    shader.setBool("uCubeEnabled", parameters.cubeEnabled);
    shader.setBool("uNoiseReduction", parameters.noiseReduction);
    shader.setFloat("uWaveHeight", parameters.waveHeight);
    shader.setFloat("uWaveFrequency", parameters.waveFrequency);
    shader.setFloat("uWaveChoppiness", parameters.waveChoppiness);
    shader.setFloat("uWaveSpeed", parameters.waveSpeed);
    shader.setFloat("uCloudCoverage", parameters.cloudCoverage);
    shader.setFloat("uReflectionStrength", parameters.reflectionStrength);
    shader.setFloat("uUnderwaterClarity", parameters.underwaterClarity);
    constexpr float degreesToRadians = 0.017453292519943295f;
    const float azimuth = parameters.sunAzimuthDegrees * degreesToRadians;
    const float elevation = parameters.sunElevationDegrees * degreesToRadians;
    shader.setVec3("uSunDirection", {std::cos(elevation) * std::sin(azimuth),
        std::sin(elevation), std::cos(elevation) * std::cos(azimuth)});
    shader.setFloat("uBloomStrength", parameters.bloomStrength);
    shader.setFloat("uExposure", parameters.exposure);
    shader.setInt("iChannel0", 0);
    shader.setInt("iChannel1", 1);
    shader.setInt("iChannel2", 2);
    shader.setInt("iChannel3", 3);
    glBindVertexArray(fullscreenVertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void EnscapeCubeRenderer::render(RenderTarget& target, const Camera& camera,
    const RendererSettings& settings, int width, int height,
    float timeSeconds, unsigned int fullscreenVertexArray) {
    resize(width, height);
    const glm::mat4 viewProjection = camera.projectionMatrix(static_cast<float>(width) / height)
        * camera.viewMatrix();
    if (!cameraValid_) {
        historyValid_ = false;
    } else {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (std::abs(viewProjection[column][row] - previousViewProjection_[column][row])
                    > 1.0e-5f) historyValid_ = false;
            }
        }
    }
    const auto& parameters = settings.enscapeCube;
    const std::array<float, 13> parameterKey{parameters.cubeEnabled ? 1.0f : 0.0f,
        parameters.noiseReduction ? 1.0f : 0.0f, parameters.waveHeight,
        parameters.waveFrequency, parameters.waveChoppiness, parameters.waveSpeed,
        parameters.cloudCoverage, parameters.reflectionStrength, parameters.underwaterClarity,
        parameters.sunAzimuthDegrees, parameters.sunElevationDegrees,
        parameters.bloomStrength, parameters.exposure};
    if (!parametersValid_ || parameterKey != previousParameters_) historyValid_ = false;
    if (std::abs(timeSeconds - previousTime_) > 0.25f) historyValid_ = false;
    gpuTimeUpdated_ = false;
    for (std::size_t index = 0; index < timingQueries_.size(); ++index) {
        if (!timingPending_[index]) continue;
        GLint available = GL_FALSE;
        glGetQueryObjectiv(timingQueries_[index], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE) {
            GLuint64 nanoseconds = 0;
            glGetQueryObjectui64v(timingQueries_[index], GL_QUERY_RESULT, &nanoseconds);
            gpuTimeMilliseconds_ = static_cast<double>(nanoseconds) / 1000000.0;
            gpuTimeValid_ = true;
            gpuTimeUpdated_ = true;
            timingPending_[index] = false;
        }
    }
    std::size_t timingIndex = timingQueries_.size();
    for (std::size_t index = 0; index < timingQueries_.size(); ++index) {
        if (!timingPending_[index]) {
            timingIndex = index;
            glBeginQuery(GL_TIME_ELAPSED, timingQueries_[index]);
            break;
        }
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, weatherTexture_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, volumeTexture_);
    draw(*shaders_[0], framebuffers_[0], width, height, timeSeconds,
        fullscreenVertexArray, camera, parameters);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
    draw(*shaders_[1], framebuffers_[1], width, height, timeSeconds,
        fullscreenVertexArray, camera, parameters);

    const unsigned int outputHistory = 2U + (historyIndex_ ^ 1U);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures_[1]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, historyValid_ ? textures_[2U + historyIndex_] : textures_[1]);
    draw(*shaders_[2], framebuffers_[outputHistory], width, height, timeSeconds,
        fullscreenVertexArray, camera, parameters);
    historyIndex_ ^= 1U;
    historyValid_ = true;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures_[2U + historyIndex_]);
    target.bindFinal();
    // Rendering into the engine's final color attachment keeps screenshots and
    // the editor viewport on their existing paths.
    GLint finalFramebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &finalFramebuffer);
    draw(*shaders_[3], static_cast<unsigned int>(finalFramebuffer), width, height,
        timeSeconds, fullscreenVertexArray, camera, parameters);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    for (int unit = 3; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);
    if (timingIndex < timingQueries_.size()) {
        glEndQuery(GL_TIME_ELAPSED);
        timingPending_[timingIndex] = true;
    }
    previousTime_ = timeSeconds;
    previousViewProjection_ = viewProjection;
    previousParameters_ = parameterKey;
    cameraValid_ = true;
    parametersValid_ = true;
}
