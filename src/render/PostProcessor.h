#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class RenderTarget;
class Shader;

struct PostProcessSettings {
    bool toneMapping{true};
    bool bloom{true};
    bool encodeSrgb{true};
    float exposure{1.0f};
    float bloomThreshold{1.0f};
    float bloomIntensity{0.12f};
    bool temporalAa{false};
    int temporalDebugView{0};
    bool resetTemporalHistory{false};
    float temporalHistoryWeight{0.9f};
    unsigned int depthTexture{0};
    unsigned int objectMotionTexture{0};
    bool outline{false};
    bool outlineNormalAvailable{false};
    float outlineWidth{1.5f};
    float outlineDepthThreshold{0.025f};
    float outlineNormalThreshold{0.25f};
    glm::vec3 outlineColor{0.025f, 0.035f, 0.055f};
    unsigned int outlineNormalTexture{0};
    bool dither{false};
    float ditherStrength{0.65f};
    bool heightFog{false};
    float heightFogDensity{0.16f};
    float heightFogBaseHeight{-0.75f};
    float heightFogFalloff{1.25f};
    glm::vec3 heightFogColor{0.32f, 0.42f, 0.58f};
    // Aerial perspective. The renderer supplies the sky model's own per-channel vertical optical
    // depth and the radiance of the sky the geometry fades into, so the compositor only has to
    // integrate the segment in front of the surface and blend. It never has to know how the sky is
    // built, which is what keeps `postprocess.frag` from growing a second copy of the model.
    bool aerialPerspective{false};
    float aerialPerspectiveStrength{1.0f};
    // Density scale height in world units. The optical depth of a horizontal ray grows by one full
    // atmospheric column per scale height travelled.
    float aerialPerspectiveScaleHeight{60.0f};
    // Optical depth of the whole vertical column, per RGB channel.
    glm::vec3 aerialPerspectiveColumnDepth{0.0f};
    glm::vec3 aerialPerspectiveZenithColor{0.0f};
    glm::vec3 aerialPerspectiveHorizonColor{0.0f};
    bool underwaterFog{false};
    glm::vec3 underwaterAbsorption{0.32f, 0.12f, 0.065f};
    glm::vec3 underwaterColor{0.012f, 0.085f, 0.12f};
    bool colorGrading{false};
    int colorGradingLut{0};
    float colorGradingStrength{1.0f};
    int stylizedDebugView{0};
    glm::vec3 cameraPosition{0.0f};
    glm::mat4 inverseProjection{1.0f};
    glm::mat4 inverseCurrentViewProjection{1.0f};
    glm::mat4 previousViewProjection{1.0f};
    // The cloud layer's `(scattered radiance, transmittance)` attachment, produced by
    // `CloudLayerRenderer` earlier in the frame. Composited here rather than drawn as its own pass
    // because this is the first point after the opaque scene exists in one texture, which is what
    // the layer has to be blended over; it also lands before tone mapping, where the layer's HDR
    // range is still intact.
    unsigned int cloudTexture{0U};
    unsigned int cloudDepthTexture{0U};
    bool cloudEnabled{false};
    unsigned int godRaysTexture{0U};
    bool godRaysEnabled{false};
    glm::vec3 godRaysColor{1.0f};
};

class PostProcessor {
public:
    PostProcessor(
        const std::filesystem::path& fullscreenVertex,
        const std::filesystem::path& extractFragment,
        const std::filesystem::path& blurFragment,
        const std::filesystem::path& compositeFragment,
        const std::filesystem::path& temporalFragment
    );
    ~PostProcessor();

    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;

    void process(RenderTarget& target, const PostProcessSettings& settings);
    std::size_t estimatedBytes() const;

private:
    void resize(int width, int height);
    void drawFullscreen() const;

    std::unique_ptr<Shader> extractShader_;
    std::unique_ptr<Shader> blurShader_;
    std::unique_ptr<Shader> compositeShader_;
    std::unique_ptr<Shader> temporalShader_;
    unsigned int vertexArray_{0};
    unsigned int framebuffers_[2]{};
    unsigned int textures_[2]{};
    unsigned int temporalFramebuffers_[2]{};
    unsigned int historyColorTextures_[2]{};
    unsigned int historyDepthTextures_[2]{};
    unsigned int motionTextures_[2]{};
    unsigned int colorGradingTextures_[3]{};
    int width_{0};
    int height_{0};
    int historyIndex_{0};
    bool historyValid_{false};
};
