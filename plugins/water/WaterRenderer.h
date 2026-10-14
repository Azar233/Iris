#pragma once
#include "plugin/RenderPlugin.h"
#include <memory>
class WaterSurface;
namespace iris {
// Owns the water grids; the backend owns shaders, draws, attachments and history.
class WaterRenderer final : public RenderPlugin {
public:
    WaterRenderer();
    ~WaterRenderer() override;
    void prepareResources(const RendererSettings&, int, int) override;
    IndexedTriangleGeometry borrowIndexedGeometry(const RendererSettings&) const override;
    std::size_t estimatedBytes() const override;
    void renderFrame(const RenderPluginFrame& frame) override;
    void invalidateHistory() override {}
    RenderPluginFrameInfo frameInfo() const override { return {}; }
private:
    std::unique_ptr<WaterSurface> high_, low_;
};
}
