#pragma once
#include <cstddef>
#include <filesystem>
#include <memory>
#include "plugin/RenderPlugin.h"
#include "render/PostProcessSettings.h"

class RenderTarget;
class Shader;

class PostProcessor final : public iris::RenderPlugin {
public:
    PostProcessor(
        const std::filesystem::path& fullscreenVertex,
        const std::filesystem::path& extractFragment,
        const std::filesystem::path& blurFragment,
        const std::filesystem::path& compositeFragment,
        const std::filesystem::path& temporalFragment
    );
    ~PostProcessor() override;

    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;

    void process(RenderTarget& target, const PostProcessSettings& settings, unsigned int hdrTexture = 0U);
    void renderFrame(const iris::RenderPluginFrame& frame) override;
    void invalidateHistory() override { historyValid_ = false; }
    iris::RenderPluginFrameInfo frameInfo() const override;
    std::size_t estimatedBytes() const override;

private:
    std::size_t lastDrawCalls_{0};
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
