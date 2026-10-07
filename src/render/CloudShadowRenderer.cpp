#include "render/CloudShadowRenderer.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glad/gl.h>
#include <glm/geometric.hpp>
#include "render/Shader.h"

CloudShadowRenderer::CloudShadowRenderer(const std::filesystem::path& directory)
    : shader_(std::make_unique<Shader>(directory / "fullscreen.vert", directory / "cloud_shadow.frag")) {
    glGenFramebuffers(1, &framebuffer_);
    glGenVertexArrays(1, &vertexArray_);
}
CloudShadowRenderer::~CloudShadowRenderer() {
    glDeleteTextures(1, &texture_);
    glDeleteFramebuffers(1, &framebuffer_);
    glDeleteVertexArrays(1, &vertexArray_);
}
glm::vec3 CloudShadowRenderer::texelRayOrigin(int x, int y) const {
    const glm::vec2 coordinate = center_ + (glm::vec2(x + 0.5f, y + 0.5f)
        / static_cast<float>(resolution_) * 2.0f - 1.0f) * extent_;
    return axisX_ * coordinate.x + axisY_ * coordinate.y;
}
void CloudShadowRenderer::render(const atmosphere::AtmosphereParameters& p,
    const glm::vec3& cameraPosition, float extent, float extinction) {
    updatedThisFrame_ = false;
    const glm::vec3 sun = atmosphere::sunDirection(p);
    active_ = p.enabled && p.cloudsEnabled && p.cloudShadowsEnabled && sun.y > 0.02f;
    if (!active_) return;
    const bool high = p.cloudQuality == atmosphere::CloudQualityTier::High;
    const int resolution = high ? 256 : 128;
    steps_ = high ? 48 : 24;
    if (resolution_ != resolution) {
        cacheValid_ = false;
        glDeleteTextures(1, &texture_);
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, resolution, resolution, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float border[] = {1,1,1,1};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Cloud shadow framebuffer is incomplete");
        resolution_ = resolution;
    }
    // Choose a robust perpendicular basis, including a sun at the zenith.
    const glm::vec3 reference = std::abs(sun.y) < 0.95f ? glm::vec3(0,1,0) : glm::vec3(0,0,1);
    axisX_ = glm::normalize(glm::cross(reference, sun));
    axisY_ = glm::normalize(glm::cross(sun, axisX_));
    extent_ = std::max(extent, 1.0f);
    const float texel = extent_ * 2.0f / static_cast<float>(resolution_);
    center_ = glm::vec2(std::round(glm::dot(cameraPosition, axisX_) / texel),
        std::round(glm::dot(cameraPosition, axisY_) / texel)) * texel;
    cloudBase_ = p.cloudBaseHeight;
    shader_->use();
    noiseTexture_.bindCanonical(*shader_,p.cloudOfflineNoise,
        static_cast<int>(std::lround(p.cloudNoisePeriod)),4);
    int program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    const std::array<float, 26> inputs{
        p.cloudBaseHeight, p.cloudTopHeight, p.cloudFeatureScale, p.cloudNoisePeriod,
        p.cloudOfflineNoise ? 1.0f : 0.0f, p.cloudWindOffsetX, p.cloudWindOffsetZ,
        p.cloudCoverage, p.cloudDensity, p.cloudWeatherScale, p.cloudCoverageVariation,
        p.cloudType, p.cloudTypeVariation, p.cloudHeightVariation,
        p.cloudDetailStrength, p.cloudDetailEdge, p.cloudShapeBlend, extent_, center_.x, center_.y,
        sun.x, sun.y, sun.z, std::max(extinction, 0.0f),
        static_cast<float>(steps_), static_cast<float>(resolution_)};
    // The snapped orthographic map depends only on these shader inputs. Keep
    // canonical resource binding/manifest validation above the reuse branch.
    // A new program after transactional hot reload also invalidates the map.
    if (cacheValid_ && cachedInputs_ == inputs && cachedProgram_ == program
        && cachedNoiseFingerprint_ == noiseTexture_.fingerprint()) return;
    shader_->setVec3("uAxisX", axisX_);
    shader_->setVec3("uAxisY", axisY_);
    shader_->setVec3("uSunDirection", sun);
    shader_->setVec2("uCenter", center_);
    shader_->setFloat("uExtent", extent_);
    shader_->setFloat("uExtinction", std::max(extinction, 0.0f));
    shader_->setInt("uSteps", steps_);
    shader_->setFloat("uLayer.baseHeight", p.cloudBaseHeight);
    shader_->setFloat("uLayer.topHeight", p.cloudTopHeight);
    shader_->setFloat("uLayer.featureScale", p.cloudFeatureScale);
    shader_->setInt("uLayer.noisePeriod", std::clamp(static_cast<int>(std::lround(p.cloudNoisePeriod)), 1, 16));
    shader_->setBool("uLayer.offlineNoise",p.cloudOfflineNoise);
    shader_->setFloat("uLayer.windX", p.cloudWindOffsetX);
    shader_->setFloat("uLayer.windZ", p.cloudWindOffsetZ);
    shader_->setFloat("uLayer.coverage", p.cloudCoverage);
    shader_->setFloat("uLayer.densityScale", p.cloudDensity);
    shader_->setFloat("uLayer.weatherScale", p.cloudWeatherScale);
    shader_->setFloat("uLayer.coverageVariation", p.cloudCoverageVariation);
    shader_->setFloat("uLayer.cloudType", p.cloudType);
    shader_->setFloat("uLayer.typeVariation", p.cloudTypeVariation);
    shader_->setFloat("uLayer.heightVariation", p.cloudHeightVariation);
    shader_->setFloat("uLayer.detailStrength", p.cloudDetailStrength);
    shader_->setFloat("uLayer.detailEdge", p.cloudDetailEdge);
    shader_->setFloat("uLayer.shapeBlend", p.cloudShapeBlend);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, resolution_, resolution_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(vertexArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    cachedInputs_ = inputs;
    cachedProgram_ = program;
    cachedNoiseFingerprint_ = noiseTexture_.fingerprint();
    cacheValid_ = true;
    updatedThisFrame_ = true;
}
void CloudShadowRenderer::bind(Shader& shader, unsigned int unit, bool enabled) const {
    shader.setBool("uCloudShadowEnabled", enabled && active_);
    shader.setInt("uCloudShadowMap", static_cast<int>(unit));
    shader.setVec3("uCloudShadowAxisX", axisX_);
    shader.setVec3("uCloudShadowAxisY", axisY_);
    shader.setVec2("uCloudShadowCenter", center_);
    shader.setFloat("uCloudShadowExtent", extent_);
    shader.setFloat("uCloudShadowBase", cloudBase_);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, enabled && active_ ? texture_ : 0U);
}
