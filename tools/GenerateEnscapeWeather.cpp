#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
std::uint32_t hash(std::uint32_t value) {
    value ^= value >> 16U; value *= 0x7feb352dU; value ^= value >> 15U; value *= 0x846ca68bU; return value ^ (value >> 16U);
}
float randomAt(int x, int y, int period) {
    const auto xx = static_cast<std::uint32_t>((x % period + period) % period);
    const auto yy = static_cast<std::uint32_t>((y % period + period) % period);
    return static_cast<float>(hash(xx + yy * 131U + 19U) & 0xffffU) / 65535.0f;
}
float noise(float x, float y, int period) {
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(xi), fy = y - static_cast<float>(yi);
    const float u = fx * fx * (3.0f - 2.0f * fx), v = fy * fy * (3.0f - 2.0f * fy);
    const float a = randomAt(xi, yi, period), b = randomAt(xi + 1, yi, period);
    const float c = randomAt(xi, yi + 1, period), d = randomAt(xi + 1, yi + 1, period);
    return (a + (b - a) * u) * (1.0f - v) + (c + (d - c) * u) * v;
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    constexpr int size = 256; std::vector<std::uint8_t> bytes(size * size);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const float nx = static_cast<float>(x) / size, ny = static_cast<float>(y) / size;
        const float value = 0.54f * noise(nx * 4.0f, ny * 4.0f, 4) + 0.30f * noise(nx * 16.0f, ny * 16.0f, 16)
            + 0.16f * noise(nx * 64.0f, ny * 64.0f, 64);
        bytes[static_cast<std::size_t>(y) * size + x] = static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
    }
    std::ofstream out(std::filesystem::u8path(argv[1]), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) return 1;
    std::cout << "Legacy weather 256x256 R8 written\n";
    return 0;
}
