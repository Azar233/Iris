#include "render/GodRaysRenderer.h"
#include <algorithm>
#include <stdexcept>
#include <glad/gl.h>
#include <glm/gtc/matrix_inverse.hpp>
#include "render/Camera.h"
#include "render/CloudShadowRenderer.h"
#include "render/Shader.h"

GodRaysRenderer::GodRaysRenderer(const std::filesystem::path& directory)
    : shader_(std::make_unique<Shader>(directory / "fullscreen.vert", directory / "god_rays.frag")) {
    glGenFramebuffers(1, &framebuffer_);
    glGenVertexArrays(1, &vertexArray_);
}
GodRaysRenderer::~GodRaysRenderer() {
    glDeleteTextures(1, &texture_);
    glDeleteFramebuffers(1, &framebuffer_);
    glDeleteVertexArrays(1, &vertexArray_);
}
bool GodRaysRenderer::render(const Camera& camera, const atmosphere::AtmosphereParameters& p,
    const CloudShadowRenderer& shadow, unsigned int cloudTexture, unsigned int depthTexture,
    int width, int height) {
    if (!p.enabled || !p.cloudsEnabled || !p.cloudGodRaysEnabled || !p.cloudShadowsEnabled
        || p.cloudGodRaysStrength <= 0 || !shadow.active() || cloudTexture == 0 || depthTexture == 0
        || camera.position().y >= p.cloudBaseHeight || width <= 0 || height <= 0) return false;
    const auto sun = atmosphere::sunDirection(p);
    const auto vp = camera.projectionMatrix(static_cast<float>(width) / height) * camera.viewMatrix();
    const glm::vec4 clip = vp * glm::vec4(sun, 0.0f);
    if (sun.y <= 0.02f || clip.w <= 0.0f) return false;
    const glm::vec2 uv = glm::vec2(clip) / clip.w * 0.5f + 0.5f;
    // Fade before leaving the viewport; never clamp a behind-camera sun to a screen edge.
    const float margin = std::min({uv.x, uv.y, 1.0f - uv.x, 1.0f - uv.y});
    if (margin <= 0.0f) return false;
    const int halfWidth = (width + 1) / 2, halfHeight = (height + 1) / 2;
    if (width_ != halfWidth || height_ != halfHeight) {
        glDeleteTextures(1, &texture_);
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, halfWidth, halfHeight, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("God rays framebuffer is incomplete");
        width_ = halfWidth; height_ = halfHeight;
    }
    shader_->use();
    shader_->setInt("uCloud", 0);
    shader_->setInt("uDepth", 1);
    shader_->setVec2("uSunUv", uv);
    shader_->setMat4("uInverseViewProjection", glm::inverse(vp));
    shader_->setVec3("uCameraPosition", camera.position());
    shader_->setFloat("uRange", camera.farPlane() * 0.5f);
    shader_->setFloat("uStrength", std::clamp(p.cloudGodRaysStrength, 0.0f, 1.0f)
        * std::clamp(margin / 0.08f, 0.0f, 1.0f));
    shader_->setInt("uSteps", p.cloudQuality == atmosphere::CloudQualityTier::High ? 48 : 24);
    shadow.bind(*shader_, 2U, true);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, cloudTexture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, depthTexture);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glBindVertexArray(vertexArray_); glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}
