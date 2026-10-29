#pragma once

#include <memory>

#include "module/ModuleRegistry.h"

// Stable ids of the modules compiled into `MyRendererModules`. Tests, the
// acceptance target and documentation refer to these constants instead of copying
// string literals.
namespace BuiltinModules {

inline constexpr const char* turntableId = "myrenderer.core.turntable";
inline constexpr const char* coastalSequenceId = "myrenderer.core.coastal-sequence";

} // namespace BuiltinModules

std::unique_ptr<ISceneModule> makeCoastalSequenceModule();
#if IRIS_ENABLE_PHYSX
std::unique_ptr<ISceneModule> makeRigidBodyModule();
#endif
