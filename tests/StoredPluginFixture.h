#pragma once
#include "plugin/RenderPluginParameters.h"

// Test-only plugin: no RendererSettings member or legacy read/write callback.
inline iris::RenderPluginParameterSchema storedPluginSchema(const std::string& id = "test.stored") {
    iris::RenderPluginParameterSchema schema;
    schema.pluginId = id;
    schema.metadata.registerBool("enabled", "Enabled output with a deliberately long parameter label", true);
    schema.metadata.registerFloat("gain", "Gain", 1.0f, 0.0f, 2.0f);
    schema.metadata.registerInt("bands", "Bands", 4, 2, 8);
    schema.metadata.registerColor("tint", "Tint", glm::vec3(0.3f, 0.6f, 0.9f));
    schema.metadata.registerEnum("preset", "Preset", {"normal", "inverted"}, 0);
    schema.validate();
    return schema;
}
