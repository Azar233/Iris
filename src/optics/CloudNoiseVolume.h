#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cloud {
// Version 1: little-endian RGBA16 UNORM, voxel centres, x fastest. RGB hold
// existing Worley body/secondary/detail; A holds independently seeded Perlin FBM.
// The payload covers [0, period)^3 and repeats on all axes.
struct NoiseVolume {
    int resolution{0};
    int period{0};
    std::vector<std::uint16_t> texels;
    std::string fingerprint() const;
    std::array<float, 4> sample(float x, float y, float z) const;
};
// Fixed, versioned runtime input. Validated once and immutable for the process,
// including CPU worker snapshots. Throws instead of silently falling back.
const NoiseVolume& canonicalNoiseVolume();
const std::filesystem::path& canonicalNoisePath();
inline constexpr const char* canonicalNoiseResource = "assets/clouds/noise-v1-64-period4.cloudnoise";
inline constexpr const char* canonicalNoiseFingerprint = "4464bd382daa06f7";
NoiseVolume generateNoiseVolume(int resolution, int period);
void writeNoiseVolume(const std::filesystem::path& path, const NoiseVolume& volume);
// Validates dimensions, exact byte count, format/generator version and content
// fingerprint before publishing anything to the caller. Throws on corruption.
NoiseVolume readNoiseVolume(const std::filesystem::path& path);
} // namespace cloud
