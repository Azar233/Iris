#pragma once
#include "plugin/RenderPlugin.h"
#include <memory>
class WaterSurface;
namespace iris {
// Owns water grids and programs; the backend owns draws, attachments and history.
class WaterRenderer final : public RenderPlugin {
public:
    explicit WaterRenderer(std::filesystem::path shaderDirectory);
    ~WaterRenderer() override;
    void prepareResources(const RendererSettings&, int, int) override;
    IndexedTriangleGeometry borrowIndexedGeometry(const RendererSettings&) const override;
    Shader* borrowShadingShader(bool deferred) const override;
    std::size_t estimatedBytes() const override;
    void renderFrame(const RenderPluginFrame& frame) override;
    void invalidateHistory() override {}
    RenderPluginFrameInfo frameInfo() const override { return {}; }
private:
    std::filesystem::path shaderDirectory_;
    std::unique_ptr<Shader> forwardShader_, deferredShader_;
    std::unique_ptr<WaterSurface> high_, low_;
};
}
