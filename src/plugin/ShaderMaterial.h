#pragma once
#include "plugin/ShaderScene.h"

namespace iris {
// Entity-local surface override; imported MaterialData and its textures remain shared.
struct ShaderMaterialBinding {
    std::string resource;
    std::vector<ModuleParameterOverride> parameters;
};
std::vector<ModuleParameterValue> shaderMaterialValues(const ShaderScenePackage&, const ShaderMaterialBinding&);
void validateShaderMaterialBinding(const ShaderMaterialBinding&);
void validateShaderMaterialSettings(const RendererSettings&);
}
