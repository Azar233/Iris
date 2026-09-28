#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace cloud {
// Versioned Beer-Lambert transport lookup, endpoint samples over optical depth
// [0,32]. Q24 integers have exact float conversion; CPU/GPU interpolate
// explicitly.
struct LightingLut {
    static constexpr int count = 8193;
    std::vector<std::uint32_t> values;
    float sample(float opticalDepth) const;
    std::string fingerprint() const;
};
LightingLut generateLightingLut();
void writeLightingLut(const std::filesystem::path&, const LightingLut&);
LightingLut readLightingLut(const std::filesystem::path&);
const LightingLut& canonicalLightingLut();
const std::filesystem::path& canonicalLightingPath();
inline constexpr const char* canonicalLightingResource = "assets/clouds/transport-v1.cloudlut";
inline constexpr const char* canonicalLightingFingerprint = "36a2741b31f43904";
float transmission(float opticalDepth, bool offline);
} // namespace cloud
