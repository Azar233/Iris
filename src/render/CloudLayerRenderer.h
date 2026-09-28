#pragma once

#include <filesystem>
#include <array>
#include <memory>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "optics/Atmosphere.h"
#include "render/CloudNoiseTexture.h"
#include "optics/CloudReference.h"

class Camera;
class Shader;

// GPU cloud volume, with optional half-resolution evaluation and independent temporal history.
// RGBA16F carries radiance/transmittance; RG32F carries first-density/entry depth in scene units.
// Only the cloud integral enters this history, never scene color or opaque depth.
//
// The march is written to match `cloud::march` term for term -- same density field, same light
// march, same centred sampling plus a world-unit jitter, same front-to-back compositing, and no
// early exit when spatial jitter is disabled. Production renders enable a deterministic pixel
// offset to suppress sampling bands; parity runs retain centred samples. The agreement measured by
// `cloud-field-parity` about the field therefore carries over to the
// integral and the GPU/CPU comparison has a chance of being tight enough to be useful.
class CloudLayerRenderer {
public:
    CloudLayerRenderer(
        const std::filesystem::path& vertexShaderPath,
        const std::filesystem::path& fragmentShaderPath
    );
    ~CloudLayerRenderer();

    CloudLayerRenderer(const CloudLayerRenderer&) = delete;
    CloudLayerRenderer& operator=(const CloudLayerRenderer&) = delete;

    // Renders the layer for the given camera and march settings. Ambient and sun radiance are
    // supplied by the shared atmosphere model; the background is composited later in PostProcessor.
    void render(
        const Camera& camera,
        const atmosphere::AtmosphereParameters& parameters,
        const cloud::MarchSettings& marchSettings,
        const glm::vec3& ambientRadiance,
        const glm::vec3& sunRadiance,
        int width,
        int height,
        bool collectDiagnostics = false
    );

    unsigned int radianceTexture() const {
        return resolved_ ? historyColor_[historyIndex_] : radianceTexture_;
    }
    unsigned int depthTexture() const {
        return resolved_ ? historyDepth_[historyIndex_] : volumeDepthTexture_;
    }
    void invalidateHistory() { historyValid_ = false; frameIndex_ = 0; }
    std::size_t noiseBytes() const { return noiseTexture_.estimatedBytes(); }
    int bufferWidth() const { return width_; }
    unsigned int rawRadianceTexture() const { return radianceTexture_; }
    int bufferHeight() const { return height_; }
    bool lastHistoryReused() const { return lastHistoryReused_; }
    // Synchronous readback is opt-in for acceptance tests only; interactive rendering never
    // downloads a full image to report a sample count. Zero means diagnostics were not collected.
    float lastMaximumSampleCount() const { return lastMaximumSampleCount_; }
    // Whether the pass ran at all this frame; a disabled layer skips the draw and the composite.
    bool lastFrameActive() const { return lastFrameActive_; }

private:
    CloudNoiseTexture noiseTexture_;
    void ensureTarget(int width, int height);
    void releaseTargets();

    std::unique_ptr<Shader> shader_;
    std::unique_ptr<Shader> temporalShader_;
    unsigned int framebuffer_{0U};
    unsigned int radianceTexture_{0U};
    unsigned int debugTexture_{0U};
    unsigned int volumeDepthTexture_{0U};
    std::array<unsigned int, 2> historyFramebuffer_{};
    std::array<unsigned int, 2> historyColor_{};
    std::array<unsigned int, 2> historyDepth_{};
    unsigned int vertexArray_{0U};
    int width_{0};
    int height_{0};
    float lastMaximumSampleCount_{0.0f};
    bool lastFrameActive_{false};
    bool historyValid_{false};
    bool lastHistoryReused_{false};
    bool resolved_{false};
    int historyIndex_{0};
    int frameIndex_{0};
    int outputWidth_{0};
    int outputHeight_{0};
    atmosphere::AtmosphereParameters previousParameters_;
    cloud::MarchSettings previousMarch_;
    glm::vec3 previousAmbient_{0.0f}, previousSun_{0.0f};
    glm::vec3 previousPosition_{0.0f}, previousForward_{0.0f};
    glm::mat4 previousViewProjection_{1.0f};
    float previousFov_{0.0f};
};
