#include "PostProcessor.h"
#include <stdexcept>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <glad/gl.h>

#include "render/RenderTarget.h"
#include "render/Shader.h"

namespace {

constexpr int colorGradingLutSize = 32;

float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

std::array<float, 3> gradeColor(int preset, float red, float green, float blue) {
    float luminance = red * 0.2126f + green * 0.7152f + blue * 0.0722f;
    if (preset == 0) {
        constexpr float saturation = 1.08f;
        red = luminance + (red - luminance) * saturation;
        green = luminance + (green - luminance) * saturation;
        blue = luminance + (blue - luminance) * saturation;
        red = (red - 0.5f) * 1.06f + 0.5f;
        green = (green - 0.5f) * 1.06f + 0.5f;
        blue = (blue - 0.5f) * 1.06f + 0.5f;
        red *= 1.015f;
        blue *= 0.985f;
    } else if (preset == 1) {
        red = std::pow(saturate(red), 0.92f);
        green = std::pow(saturate(green), 0.92f);
        blue = std::pow(saturate(blue), 0.92f);
        luminance = red * 0.2126f + green * 0.7152f + blue * 0.0722f;
        constexpr float saturation = 0.88f;
        red = luminance + (red - luminance) * saturation;
        green = luminance + (green - luminance) * saturation;
        blue = luminance + (blue - luminance) * saturation;
        const float shadow = 1.0f - luminance;
        red += 0.015f * shadow + 0.045f * luminance;
        green += 0.035f * shadow + 0.025f * luminance;
        blue += 0.050f * shadow;
    } else {
        red = std::pow(saturate(red), 1.08f) * 0.80f;
        green = std::pow(saturate(green), 1.04f) * 0.93f;
        blue = std::pow(saturate(blue), 0.96f) * 1.12f;
        luminance = red * 0.2126f + green * 0.7152f + blue * 0.0722f;
        constexpr float saturation = 1.18f;
        red = luminance + (red - luminance) * saturation;
        green = luminance + (green - luminance) * saturation;
        blue = luminance + (blue - luminance) * saturation;
        const float shadow = 1.0f - saturate(luminance);
        red += 0.018f * shadow + 0.035f * luminance;
        green += 0.045f * shadow + 0.020f * luminance;
        blue += 0.110f * shadow + 0.045f * luminance;
    }
    return {saturate(red), saturate(green), saturate(blue)};
}

std::vector<float> makeColorGradingLut(int preset) {
    std::vector<float> values;
    values.reserve(
        static_cast<std::size_t>(colorGradingLutSize)
        * static_cast<std::size_t>(colorGradingLutSize)
        * static_cast<std::size_t>(colorGradingLutSize) * 3U
    );
    const float denominator = static_cast<float>(colorGradingLutSize - 1);
    for (int blue = 0; blue < colorGradingLutSize; ++blue) {
        for (int green = 0; green < colorGradingLutSize; ++green) {
            for (int red = 0; red < colorGradingLutSize; ++red) {
                const std::array<float, 3> graded = gradeColor(
                    preset,
                    static_cast<float>(red) / denominator,
                    static_cast<float>(green) / denominator,
                    static_cast<float>(blue) / denominator
                );
                values.insert(values.end(), graded.begin(), graded.end());
            }
        }
    }
    return values;
}

void configureTexture(
    unsigned int texture,
    int internalFormat,
    int format,
    int type,
    int width,
    int height,
    int filtering
) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

} // namespace

PostProcessor::PostProcessor(
    const std::filesystem::path& fullscreenVertex,
    const std::filesystem::path& extractFragment,
    const std::filesystem::path& blurFragment,
    const std::filesystem::path& compositeFragment,
    const std::filesystem::path& temporalFragment
) : extractShader_(std::make_unique<Shader>(fullscreenVertex, extractFragment)),
    blurShader_(std::make_unique<Shader>(fullscreenVertex, blurFragment)),
    compositeShader_(std::make_unique<Shader>(fullscreenVertex, compositeFragment)),
    temporalShader_(std::make_unique<Shader>(fullscreenVertex, temporalFragment)) {
    glGenVertexArrays(1, &vertexArray_);
    glGenFramebuffers(2, framebuffers_);
    glGenTextures(2, textures_);
    glGenFramebuffers(2, temporalFramebuffers_);
    glGenTextures(2, historyColorTextures_);
    glGenTextures(2, historyDepthTextures_);
    glGenTextures(2, motionTextures_);
    glGenTextures(3, colorGradingTextures_);
    for (int preset = 0; preset < 3; ++preset) {
        const std::vector<float> lut = makeColorGradingLut(preset);
        glBindTexture(GL_TEXTURE_3D, colorGradingTextures_[preset]);
        glTexImage3D(
            GL_TEXTURE_3D, 0, GL_RGB16F,
            colorGradingLutSize, colorGradingLutSize, colorGradingLutSize,
            0, GL_RGB, GL_FLOAT, lut.data()
        );
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_3D, 0);
}

PostProcessor::~PostProcessor() {
    glDeleteTextures(3, colorGradingTextures_);
    glDeleteTextures(2, motionTextures_);
    glDeleteTextures(2, historyDepthTextures_);
    glDeleteTextures(2, historyColorTextures_);
    glDeleteFramebuffers(2, temporalFramebuffers_);
    glDeleteTextures(2, textures_);
    glDeleteFramebuffers(2, framebuffers_);
    if (vertexArray_ != 0U) glDeleteVertexArrays(1, &vertexArray_);
}

std::size_t PostProcessor::estimatedBytes() const {
    if (width_ <= 0 || height_ <= 0) return 0U;
    return static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)
        * (8U * 2U + (8U + 4U + 4U) * 2U)
        + static_cast<std::size_t>(colorGradingLutSize * colorGradingLutSize
            * colorGradingLutSize * 3 * 2 * 3);
}

void PostProcessor::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    historyValid_ = false;
    for (int index = 0; index < 2; ++index) {
        glBindTexture(GL_TEXTURE_2D, textures_[index]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[index]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures_[index], 0);

        configureTexture(
            historyColorTextures_[index], GL_RGBA16F, GL_RGBA, GL_FLOAT,
            width_, height_, GL_LINEAR
        );
        configureTexture(
            historyDepthTextures_[index], GL_R32F, GL_RED, GL_FLOAT,
            width_, height_, GL_NEAREST
        );
        configureTexture(
            motionTextures_[index], GL_RG16F, GL_RG, GL_FLOAT,
            width_, height_, GL_LINEAR
        );
        glBindFramebuffer(GL_FRAMEBUFFER, temporalFramebuffers_[index]);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
            historyColorTextures_[index], 0
        );
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
            historyDepthTextures_[index], 0
        );
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D,
            motionTextures_[index], 0
        );
        constexpr unsigned int attachments[]{
            GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2
        };
        glDrawBuffers(3, attachments);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcessor::drawFullscreen() const {
    glBindVertexArray(vertexArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void PostProcessor::renderFrame(const iris::RenderPluginFrame& frame) {
    if (!frame.postProcessSettings) throw std::invalid_argument("Postprocess settings missing");
    iris::validatePluginBindings(iris::postProcessContract(), frame.textures, frame.width, frame.height);
    auto settings = *frame.postProcessSettings;
    if (frame.texture("display") != frame.target.colorTexture())
        throw std::invalid_argument("Postprocess output does not match borrowed target");
    settings.depthTexture = frame.texture("depth");
    settings.objectMotionTexture = frame.texture("motion");
    settings.outlineNormalTexture = frame.texture("normals");
    settings.opaqueDepthTexture = frame.texture("opaqueDepth");
    if (settings.temporalAa && settings.depthTexture == 0U)
        throw std::invalid_argument("Postprocess TAA requires depth");
    if (settings.outlineNormalAvailable && settings.outlineNormalTexture == 0U)
        throw std::invalid_argument("Postprocess normal input missing");
    lastDrawCalls_ = 1U + (settings.bloom ? 9U : 0U) + (settings.temporalAa ? 1U : 0U);
    process(frame.target, settings, frame.texture("hdr"));
}

iris::RenderPluginFrameInfo PostProcessor::frameInfo() const {
    return {{"Postprocess temporal", "Postprocess bloom", "Postprocess composite"},
        lastDrawCalls_, false, false, 0.0};
}

void PostProcessor::process(RenderTarget& target, const PostProcessSettings& settings, unsigned int hdrTexture) {
    resize(target.width(), target.height());
    glViewport(0, 0, target.width(), target.height());
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    const unsigned int sourceHdr = hdrTexture != 0U ? hdrTexture : target.hdrColorTexture();
    unsigned int sceneTexture = sourceHdr;
    unsigned int motionTexture = 0U;
    if (settings.temporalAa) {
        if (settings.resetTemporalHistory) historyValid_ = false;
        const int destination = 1 - historyIndex_;
        glBindFramebuffer(GL_FRAMEBUFFER, temporalFramebuffers_[destination]);
        temporalShader_->use();
        temporalShader_->setInt("uCurrentColor", 0);
        temporalShader_->setInt("uCurrentDepth", 1);
        temporalShader_->setInt("uHistoryColor", 2);
        temporalShader_->setInt("uHistoryDepth", 3);
        temporalShader_->setInt("uObjectMotion", 4);
        temporalShader_->setBool("uObjectMotionAvailable", settings.objectMotionTexture != 0U);
        temporalShader_->setBool("uHistoryValid", historyValid_);
        temporalShader_->setFloat(
            "uHistoryWeight",
            std::clamp(settings.temporalHistoryWeight, 0.0f, 0.98f)
        );
        temporalShader_->setMat4(
            "uInverseCurrentViewProjection",
            settings.inverseCurrentViewProjection
        );
        temporalShader_->setMat4("uPreviousViewProjection", settings.previousViewProjection);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sourceHdr);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, settings.depthTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, historyColorTextures_[historyIndex_]);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, historyDepthTextures_[historyIndex_]);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, settings.objectMotionTexture);
        drawFullscreen();
        historyIndex_ = destination;
        historyValid_ = true;
        sceneTexture = historyColorTextures_[historyIndex_];
        motionTexture = motionTextures_[historyIndex_];
    } else {
        historyValid_ = false;
    }

    int bloomTextureIndex = 0;
    if (settings.bloom) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[0]);
        extractShader_->use();
        extractShader_->setInt("uScene", 0);
        extractShader_->setFloat("uThreshold", settings.bloomThreshold);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneTexture);
        drawFullscreen();

        bool horizontal = true;
        for (int iteration = 0; iteration < 8; ++iteration) {
            const int destination = horizontal ? 1 : 0;
            const int source = horizontal ? 0 : 1;
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[destination]);
            blurShader_->use();
            blurShader_->setInt("uImage", 0);
            blurShader_->setBool("uHorizontal", horizontal);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textures_[source]);
            drawFullscreen();
            bloomTextureIndex = destination;
            horizontal = !horizontal;
        }
    }

    target.bindFinal();
    compositeShader_->use();
    compositeShader_->setInt("uScene", 0);
    compositeShader_->setInt("uBloom", 1);
    compositeShader_->setInt("uMotion", 2);
    compositeShader_->setInt("uDepth", 3);
    compositeShader_->setInt("uEncodedNormal", 4);
    compositeShader_->setInt("uColorGradingLut", 5);
    compositeShader_->setInt("uTemporalDebugView", settings.temporalDebugView);
    compositeShader_->setBool("uToneMapping", settings.toneMapping);
    compositeShader_->setBool("uBloomEnabled", settings.bloom);
    compositeShader_->setBool("uEncodeSrgb", settings.encodeSrgb);
    compositeShader_->setFloat("uExposure", settings.exposure);
    compositeShader_->setFloat("uBloomIntensity", settings.bloomIntensity);
    compositeShader_->setBool("uOutlineEnabled", settings.outline);
    compositeShader_->setBool(
        "uOutlineNormalAvailable", settings.outlineNormalAvailable
    );
    compositeShader_->setFloat(
        "uOutlineWidth", std::clamp(settings.outlineWidth, 0.5f, 6.0f)
    );
    compositeShader_->setFloat(
        "uOutlineDepthThreshold",
        std::clamp(settings.outlineDepthThreshold, 0.001f, 0.25f)
    );
    compositeShader_->setFloat(
        "uOutlineNormalThreshold",
        std::clamp(settings.outlineNormalThreshold, 0.01f, 1.0f)
    );
    compositeShader_->setVec3("uOutlineColor", settings.outlineColor);
    compositeShader_->setBool("uDitherEnabled", settings.dither);
    compositeShader_->setFloat(
        "uDitherStrength", std::clamp(settings.ditherStrength, 0.0f, 1.0f)
    );
    compositeShader_->setBool("uHeightFogEnabled", settings.heightFog);
    compositeShader_->setFloat(
        "uHeightFogDensity", std::clamp(settings.heightFogDensity, 0.0f, 2.0f)
    );
    compositeShader_->setFloat("uHeightFogBaseHeight", settings.heightFogBaseHeight);
    compositeShader_->setFloat(
        "uHeightFogFalloff", std::clamp(settings.heightFogFalloff, 0.01f, 4.0f)
    );
    compositeShader_->setVec3("uHeightFogColor", settings.heightFogColor);
    compositeShader_->setBool("uAerialEnabled", settings.aerialPerspective);
    compositeShader_->setFloat(
        "uAerialStrength", std::clamp(settings.aerialPerspectiveStrength, 0.0f, 4.0f)
    );
    compositeShader_->setFloat(
        "uAerialScaleHeight", std::max(settings.aerialPerspectiveScaleHeight, 0.01f)
    );
    compositeShader_->setVec3(
        "uAerialColumnDepth",
        glm::max(settings.aerialPerspectiveColumnDepth, glm::vec3(0.0f))
    );
    compositeShader_->setVec3(
        "uAerialZenithColor", glm::max(settings.aerialPerspectiveZenithColor, glm::vec3(0.0f))
    );
    compositeShader_->setVec3(
        "uAerialHorizonColor", glm::max(settings.aerialPerspectiveHorizonColor, glm::vec3(0.0f))
    );
    compositeShader_->setBool("uUnderwaterFogEnabled", settings.underwaterFog);
    compositeShader_->setBool("uWaterSurfaceOptics", settings.waterSurfaceOptics);
    compositeShader_->setFloat("uWaterSurfaceHeight", settings.waterSurfaceHeight);
    compositeShader_->setInt("uOpaqueDepth", 9);
    compositeShader_->setVec3("uUnderwaterAbsorption", settings.underwaterAbsorption);
    compositeShader_->setVec3("uUnderwaterColor", settings.underwaterColor);
    compositeShader_->setBool("uColorGradingEnabled", settings.colorGrading);
    compositeShader_->setFloat(
        "uColorGradingStrength", std::clamp(settings.colorGradingStrength, 0.0f, 1.0f)
    );
    compositeShader_->setInt("uStylizedDebugView", settings.stylizedDebugView);
    compositeShader_->setFloat(
        "uInverseWidth", 1.0f / static_cast<float>(target.width())
    );
    compositeShader_->setFloat(
        "uInverseHeight", 1.0f / static_cast<float>(target.height())
    );
    compositeShader_->setMat4("uInverseProjection", settings.inverseProjection);
    compositeShader_->setMat4(
        "uInverseCurrentViewProjection", settings.inverseCurrentViewProjection
    );
    compositeShader_->setVec3("uCameraPosition", settings.cameraPosition);
    // Unit 6 carries the cloud layer. Its transmittance is the alpha channel, so the composite is a
    // plain "radiance over the scene" blend where the scene is what the layer was marched against.
    compositeShader_->setInt("uCloud", 6);
    compositeShader_->setInt("uCloudDepth", 7);
    compositeShader_->setBool("uCloudEnabled", settings.cloudEnabled);
    compositeShader_->setInt("uGodRays", 8);
    compositeShader_->setBool("uGodRaysEnabled", settings.godRaysEnabled);
    compositeShader_->setVec3("uGodRaysColor", settings.godRaysColor);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures_[bloomTextureIndex]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, motionTexture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, settings.depthTexture);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, settings.outlineNormalTexture);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(
        GL_TEXTURE_3D,
        colorGradingTextures_[std::clamp(settings.colorGradingLut, 0, 2)]
    );
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, settings.cloudTexture);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, settings.cloudDepthTexture);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, settings.godRaysTexture);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, settings.opaqueDepthTexture);
    drawFullscreen();
    target.unbind();
}
