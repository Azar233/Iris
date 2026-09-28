#include "render/CloudLayerRenderer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <glad/gl.h>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "render/Camera.h"
#include "render/Shader.h"

CloudLayerRenderer::CloudLayerRenderer(
    const std::filesystem::path& vertexShaderPath,
    const std::filesystem::path& fragmentShaderPath
) {
    // A Core Profile context requires a bound vertex array object even when the vertex shader
    // generates its positions from `gl_VertexID`; without one the draw is rejected on some drivers
    // and silently accepted on others.
    glGenVertexArrays(1, &vertexArray_);
    shader_ = std::make_unique<Shader>(vertexShaderPath, fragmentShaderPath);
    temporalShader_ = std::make_unique<Shader>(vertexShaderPath,
        fragmentShaderPath.parent_path() / "cloud_temporal.frag");
    glGenFramebuffers(1, &framebuffer_);
}

CloudLayerRenderer::~CloudLayerRenderer() {
    releaseTargets();
    if (framebuffer_ != 0U) glDeleteFramebuffers(1, &framebuffer_);
    if (vertexArray_ != 0U) glDeleteVertexArrays(1, &vertexArray_);
}

void CloudLayerRenderer::releaseTargets() {
    glDeleteTextures(1, &radianceTexture_);
    glDeleteTextures(1, &debugTexture_);
    glDeleteTextures(1, &volumeDepthTexture_);
    glDeleteTextures(2, historyColor_.data());
    glDeleteTextures(2, historyDepth_.data());
    glDeleteFramebuffers(2, historyFramebuffer_.data());
    radianceTexture_ = debugTexture_ = volumeDepthTexture_ = 0U;
    historyColor_.fill(0U);
    historyDepth_.fill(0U);
    historyFramebuffer_.fill(0U);
    invalidateHistory();
}

void CloudLayerRenderer::ensureTarget(int width, int height) {
    if (width == width_ && height == height_ && radianceTexture_ != 0U) return;
    releaseTargets();
    width_ = width;
    height_ = height;
    glGenTextures(1, &radianceTexture_);
    glGenTextures(1, &debugTexture_);
    glGenTextures(1, &volumeDepthTexture_);
    glGenTextures(2, historyColor_.data());
    glGenTextures(2, historyDepth_.data());
    glGenFramebuffers(2, historyFramebuffer_.data());
    for (unsigned int texture : {radianceTexture_, debugTexture_}) {
        glBindTexture(GL_TEXTURE_2D, texture);
        // RGBA16F rather than RGBA8: the pass runs in linear HDR before tone mapping, so an 8-bit
        // target would clip a silver lining and quantise the transmittance the composite depends on.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    for (unsigned int texture : {volumeDepthTexture_, historyDepth_[0], historyDepth_[1]}) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, width, height, 0, GL_RG, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    for (int index = 0; index < 2; ++index) {
        glBindTexture(GL_TEXTURE_2D, historyColor_[index]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, historyFramebuffer_[index]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, historyColor_[index], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, historyDepth_[index], 0);
        const GLenum outputs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, outputs);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Cloud history framebuffer is incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        radianceTexture_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, debugTexture_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, volumeDepthTexture_, 0);
    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, attachments);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "Cloud layer framebuffer is incomplete\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CloudLayerRenderer::render(
    const Camera& camera,
    const atmosphere::AtmosphereParameters& parameters,
    const cloud::MarchSettings& marchSettings,
    const glm::vec3& ambientRadiance,
    const glm::vec3& sunRadiance,
    int width,
    int height,
    bool collectDiagnostics
) {
    lastMaximumSampleCount_ = 0.0f;
    resolved_ = false;
    lastHistoryReused_ = false;
    lastFrameActive_ = parameters.enabled && parameters.cloudsEnabled
        && marchSettings.primarySteps > 0;
    if (!lastFrameActive_ || shader_ == nullptr) { invalidateHistory(); return; }
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (outputWidth_ != width || outputHeight_ != height) invalidateHistory();
    outputWidth_ = width;
    outputHeight_ = height;
    ensureTarget(parameters.cloudHalfResolution ? (width + 1) / 2 : width,
        parameters.cloudHalfResolution ? (height + 1) / 2 : height);

    auto comparable = previousParameters_;
    comparable.cloudWindOffsetX = parameters.cloudWindOffsetX;
    comparable.cloudWindOffsetZ = parameters.cloudWindOffsetZ;
    const glm::vec2 windDelta(parameters.cloudWindOffsetX - previousParameters_.cloudWindOffsetX,
        parameters.cloudWindOffsetZ - previousParameters_.cloudWindOffsetZ);
    const bool cameraCut = glm::dot(camera.forwardDirection(), previousForward_) < 0.9f
        || glm::length(camera.position() - previousPosition_) > parameters.cloudFeatureScale * 0.25f
        || std::abs(camera.fieldOfView() - previousFov_) > 0.001f;
    const bool marchChanged = marchSettings.primarySteps != previousMarch_.primarySteps
        || marchSettings.lightSteps != previousMarch_.lightSteps
        || marchSettings.extinction != previousMarch_.extinction
        || marchSettings.spatialJitter != previousMarch_.spatialJitter
        || marchSettings.jitter != previousMarch_.jitter
        || marchSettings.multiScatterOctaves != previousMarch_.multiScatterOctaves
        || marchSettings.multiScatterAttenuation != previousMarch_.multiScatterAttenuation
        || marchSettings.multiScatterEccentricity != previousMarch_.multiScatterEccentricity
        || marchSettings.powder != previousMarch_.powder;
    if (!parameters.cloudTemporalEnabled || !atmosphere::parametersMatch(comparable, parameters)
        || cameraCut || marchChanged || glm::length(windDelta) > parameters.cloudFeatureScale * 0.25f
        || glm::length(ambientRadiance - previousAmbient_) > 0.001f
        || glm::length(sunRadiance - previousSun_) > 0.001f) invalidateHistory();

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float halfHeight = std::tan(glm::radians(camera.fieldOfView()) * 0.5f);
    const glm::vec3 sunDirection = atmosphere::sunDirection(parameters);

    shader_->use();
    shader_->setMat4("uInverseViewProjection", glm::inverse(
        camera.projectionMatrix(aspect) * camera.viewMatrix()));
    shader_->setVec3("uCameraPosition", camera.position());
    shader_->setVec3("uCameraForward", camera.forwardDirection());
    shader_->setVec3("uCameraRight", camera.rightDirection());
    shader_->setVec3("uCameraUp", camera.upDirection());
    shader_->setVec2("uHalfExtent", glm::vec2(halfHeight * aspect, halfHeight));
    shader_->setVec3("uAmbientRadiance", ambientRadiance);
    shader_->setVec3("uSunRadiance", sunRadiance);
    shader_->setVec3("uSunDirection", sunDirection);
    shader_->setFloat("uExtinction", marchSettings.extinction);
    shader_->setFloat("uBaseHeight", parameters.cloudBaseHeight);
    shader_->setFloat("uTopHeight", parameters.cloudTopHeight);
    shader_->setFloat("uFeatureScale", parameters.cloudFeatureScale);
    shader_->setInt("uNoisePeriod", std::clamp(
        static_cast<int>(std::lround(parameters.cloudNoisePeriod)), 1, 16));
    shader_->setBool("uOfflineNoise",parameters.cloudOfflineNoise);
    noiseTexture_.bindCanonical(*shader_,parameters.cloudOfflineNoise,
        static_cast<int>(std::lround(parameters.cloudNoisePeriod)),4);
    shader_->setVec2("uWind", glm::vec2(parameters.cloudWindOffsetX, parameters.cloudWindOffsetZ));
    shader_->setFloat("uCoverage", parameters.cloudCoverage);
    shader_->setFloat("uDensityScale", parameters.cloudDensity);
    // The weather map's controls (C5). They travel individually rather than as one packed vector
    // because they are read by name in the shader's struct assembly, and a packed upload is exactly
    // how the `setVec3`-into-a-`vec2` bug happened the first time.
    shader_->setFloat("uWeatherScale", parameters.cloudWeatherScale);
    shader_->setFloat("uCoverageVariation", parameters.cloudCoverageVariation);
    shader_->setFloat("uCloudType", parameters.cloudType);
    shader_->setFloat("uTypeVariation", parameters.cloudTypeVariation);
    shader_->setFloat("uHeightVariation", parameters.cloudHeightVariation);
    shader_->setFloat("uDetailStrength", parameters.cloudDetailStrength);
    shader_->setFloat("uDetailEdge", parameters.cloudDetailEdge);
    shader_->setFloat("uMaximumViewDistance", cloud::maximumViewDistance(parameters));
    shader_->setFloat("uHorizonFadeDegrees", parameters.cloudHorizonFadeDegrees);
    shader_->setInt("uPrimarySteps", marchSettings.primarySteps);
    shader_->setInt("uLightSteps", marchSettings.lightSteps);
    shader_->setFloat("uJitter", marchSettings.jitter);
    shader_->setBool("uSpatialJitter", marchSettings.spatialJitter);
    shader_->setInt("uJitterFrame", parameters.cloudTemporalEnabled ? frameIndex_ : 0);
    // The multiple-scattering octaves have to reach the shader for the same reason the step budgets
    // do: the CPU reference is the value this pass is compared against, and a term present on one
    // side only would make that comparison meaningless rather than merely loose.
    shader_->setInt("uMultiScatterOctaves", marchSettings.multiScatterOctaves);
    shader_->setFloat("uMultiScatterAttenuation", marchSettings.multiScatterAttenuation);
    shader_->setFloat("uMultiScatterEccentricity", marchSettings.multiScatterEccentricity);
    shader_->setBool("uPowder", marchSettings.powder);
    shader_->setInt("uDepth", 0);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(vertexArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (parameters.cloudTemporalEnabled) {
        const int destination = 1 - historyIndex_;
        temporalShader_->use();
        temporalShader_->setInt("uCurrent", 0);
        temporalShader_->setInt("uVolumeDepth", 1);
        temporalShader_->setInt("uHistory", 2);
        temporalShader_->setInt("uHistoryDepth", 3);
        temporalShader_->setBool("uHistoryValid", historyValid_);
        temporalShader_->setVec3("uCameraPosition", camera.position());
        temporalShader_->setVec3("uCameraForward", camera.forwardDirection());
        temporalShader_->setVec3("uCameraRight", camera.rightDirection());
        temporalShader_->setVec3("uCameraUp", camera.upDirection());
        temporalShader_->setVec2("uHalfExtent", glm::vec2(halfHeight * aspect, halfHeight));
        temporalShader_->setVec3("uPreviousPosition", previousPosition_);
        temporalShader_->setMat4("uPreviousViewProjection", previousViewProjection_);
        temporalShader_->setVec2("uWindDelta", windDelta);
        const unsigned int textures[] = {radianceTexture_, volumeDepthTexture_,
            historyColor_[historyIndex_], historyDepth_[historyIndex_]};
        for (int unit = 0; unit < 4; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, textures[unit]);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, historyFramebuffer_[destination]);
        glBindVertexArray(vertexArray_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        lastHistoryReused_ = historyValid_;
        historyIndex_ = destination;
        resolved_ = true;
        historyValid_ = true;
        frameIndex_ = (frameIndex_ + 1) % 1024;
    }
    previousParameters_ = parameters;
    previousMarch_ = marchSettings;
    previousAmbient_ = ambientRadiance;
    previousSun_ = sunRadiance;
    previousPosition_ = camera.position();
    previousForward_ = camera.forwardDirection();
    previousFov_ = camera.fieldOfView();
    previousViewProjection_ = camera.projectionMatrix(aspect) * camera.viewMatrix();

    // The GPU reports its own sample count, so the cost can be compared against the CPU reference's
    // `densitySampleCount` instead of being asserted from the source.
    if (collectDiagnostics && debugTexture_ != 0U) {
        // `glGetTexImage` always reads the *whole* mip level, not one texel, so the destination has to
        // be `width * height * 4` floats. Sizing it as one texel is a stack overrun that the call
        // itself reports no error for -- a lesson this pass already paid for twice.
        std::vector<float> pixels(static_cast<std::size_t>(width_) * height_ * 4U, 0.0f);
        glBindTexture(GL_TEXTURE_2D, debugTexture_);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
        float maximum = 0.0f;
        for (std::size_t index = 0U; index < pixels.size(); index += 4U) {
            maximum = std::max(maximum, pixels[index]);
        }
        lastMaximumSampleCount_ = maximum;
    }
}
