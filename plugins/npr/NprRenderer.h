#pragma once
#include "plugin/RenderPlugin.h"

namespace iris {
// Stateless uniform adapter. Shader programs, draw passes and history belong
// to the backend; NPR post effects still belong to the PostProcess plugin.
class NprRenderer final : public RenderPlugin {
public:
    void renderFrame(const RenderPluginFrame& frame) override;
    void invalidateHistory() override {}
    RenderPluginFrameInfo frameInfo() const override { return {}; }
};
}
