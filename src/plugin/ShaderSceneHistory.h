#pragma once
#include <cmath>
#include <string>

namespace iris {
inline constexpr float shaderSceneMaximumTimeStep = 0.25f;
struct ShaderSceneHistoryPolicy {
    bool resetOnParameters{true};
    bool resetOnCamera{true};
    float maximumTimeStep{shaderSceneMaximumTimeStep};
};
inline bool operator==(const ShaderSceneHistoryPolicy& a, const ShaderSceneHistoryPolicy& b) {
    return a.resetOnParameters == b.resetOnParameters && a.resetOnCamera == b.resetOnCamera
        && a.maximumTimeStep == b.maximumTimeStep;
}
inline bool operator!=(const ShaderSceneHistoryPolicy& a, const ShaderSceneHistoryPolicy& b) { return !(a == b); }
inline bool validateShaderSceneHistoryPolicy(const ShaderSceneHistoryPolicy& policy, std::string& error) {
    if (!std::isfinite(policy.maximumTimeStep) || policy.maximumTimeStep < .001f || policy.maximumTimeStep > 10.f) {
        error = "ShaderScene history maximumTimeStep must be finite and within 0.001..10 seconds"; return false;
    }
    error.clear(); return true;
}
inline bool updateShaderSceneHistoryPolicy(ShaderSceneHistoryPolicy& stored,
    const ShaderSceneHistoryPolicy& requested, bool& changed, std::string& error) {
    changed = false;
    if (!validateShaderSceneHistoryPolicy(requested, error)) return false;
    changed = stored != requested;
    stored = requested; return true;
}
}
