#pragma once
#include "plugin/RenderPlugin.h"
#include "plugin/ShaderScene.h"
#include <memory>
class Shader;
namespace iris {
class ShaderSceneRenderer final : public RenderPlugin {
public:
    explicit ShaderSceneRenderer(const std::filesystem::path& shaderDirectory);
    ~ShaderSceneRenderer() override;
    void prepareResources(const RendererSettings& settings, int width, int height) override;
    void renderFrame(const RenderPluginFrame& frame) override;
    void invalidateHistory() override {}
    RenderPluginFrameInfo frameInfo() const override;
private:
    std::filesystem::path vertex_;
    ShaderScenePackage package_;
    std::unique_ptr<Shader> shader_;
};
}
