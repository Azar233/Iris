#pragma once
#include "plugin/RenderPlugin.h"
namespace iris {
// Stateless uniform adapter; the backend owns meshes, shaders, attachments and history.
class WaterRenderer final : public RenderPlugin {
public:
    void renderFrame(const RenderPluginFrame& frame) override;
    void invalidateHistory() override {}
    RenderPluginFrameInfo frameInfo() const override { return {}; }
};
}
