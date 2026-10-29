#pragma once
#include <cstdint>

namespace pathtracer {
// Product bound shared by Job validation, GPU dispatch and viewport scaling.
// Device dispatch/storage limits can reject an otherwise supported extent.
inline constexpr std::uint32_t gpuMaximumDimension = 1280;
}
