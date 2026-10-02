#include "render/ShadowMap.h"

#include <algorithm>
#include <stdexcept>

#include <glad/gl.h>

ShadowMap::ShadowMap(int resolution, int layers)
    : resolution_(std::max(resolution, 1)), layers_(std::clamp(layers, 1, 8)) {
    glGenFramebuffers(1, &framebuffer_);
    glGenTextures(1, &depthTexture_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, depthTexture_);
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
        resolution_, resolution_, layers_, 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
    );
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    // Layer 0 is attached up front so the framebuffer is complete without a bind call first, and so
    // the attachment state matches `attachedLayer_`.
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTexture_, 0, 0);
    attachedLayer_ = 0;
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Failed to create cascaded shadow framebuffer");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenFramebuffers(1, &transmissionFramebuffer_);
    glGenTextures(1, &transmissionTexture_);
    glBindTexture(GL_TEXTURE_2D, transmissionTexture_);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA16F, resolution_, resolution_, 0,
        GL_RGBA, GL_FLOAT, nullptr
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindFramebuffer(GL_FRAMEBUFFER, transmissionFramebuffer_);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        transmissionTexture_,
        0
    );
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Failed to create transmission shadow framebuffer");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

ShadowMap::~ShadowMap() {
    if (transmissionTexture_ != 0U) glDeleteTextures(1, &transmissionTexture_);
    if (transmissionFramebuffer_ != 0U) glDeleteFramebuffers(1, &transmissionFramebuffer_);
    if (depthTexture_ != 0U) glDeleteTextures(1, &depthTexture_);
    if (framebuffer_ != 0U) glDeleteFramebuffers(1, &framebuffer_);
}

void ShadowMap::bindTransmissionForWriting() const {
    glBindFramebuffer(GL_FRAMEBUFFER, transmissionFramebuffer_);
}

void ShadowMap::bindForWriting(std::size_t layer) const {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    const int clamped = static_cast<int>(
        std::min<std::size_t>(layer, static_cast<std::size_t>(layers_ - 1))
    );
    if (clamped == attachedLayer_) return;
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTexture_, 0, clamped);
    attachedLayer_ = clamped;
}

void ShadowMap::bindTransmissionTexture(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, transmissionTexture_);
}

void ShadowMap::bindTexture(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, depthTexture_);
}
