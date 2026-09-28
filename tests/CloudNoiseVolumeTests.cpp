#include "optics/CloudNoiseVolume.h"
#include "optics/CloudFieldCpp.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename F> void rejects(F function, const char* message) {
    bool failed = false; try { function(); } catch (const std::exception&) { failed = true; }
    require(failed, message);
}
}
int main() {
    const auto directory = std::filesystem::temp_directory_path() / ("myrenderer-cloud-noise-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(directory);
        const auto asset = directory / "test.cloudnoise";
        auto volume = cloud::generateNoiseVolume(32, 4);
        require(volume.fingerprint() == cloud::generateNoiseVolume(32, 4).fingerprint(), "Generation must repeat");
        require(volume.fingerprint() != cloud::generateNoiseVolume(32, 5).fingerprint(), "Period must enter content identity");
        cloud::writeNoiseVolume(asset, volume);
        require(cloud::readNoiseVolume(asset).texels == volume.texels, "Little-endian round trip must preserve every texel");
        for (float p : {-5.0f, -4.0001f, -0.0001f, 0.0f, 0.0001f, 3.9999f, 4.0f}) {
            const auto a = volume.sample(p, p * 0.5f, -p);
            const auto b = volume.sample(p + 4.0f, p * 0.5f - 4.0f, -p + 4.0f);
            for (std::size_t c = 0; c < 4; ++c) require(std::abs(a[c] - b[c]) < 2.0e-6f, "All axes and negative seams must tile");
        }
        const auto centre = volume.sample(0.0625f, 0.0625f, 0.0625f);
        for (std::size_t c = 0; c < 4; ++c) require(centre[c] == static_cast<float>(volume.texels[c]) / 65535.0f,
            "Voxel centre must reproduce stored sample exactly");
        const auto left = volume.sample(-0.00001f, 1.125f, 2.125f);
        const auto right = volume.sample(0.00001f, 1.125f, 2.125f);
        for (std::size_t c = 0; c < 4; ++c) require(std::abs(left[c]-right[c]) < 0.0001f, "Filtered repeat seam must be continuous");
        rejects([]{ cloud::generateNoiseVolume(4096, 4); }, "Unbounded allocations must reject");
        rejects([]{ cloud::generateNoiseVolume(32, 0); }, "Invalid period must reject");
        rejects([&]{ volume.sample(INFINITY, 0, 0); }, "Nonfinite coordinates must reject");
        { std::fstream file(asset,std::ios::binary|std::ios::in|std::ios::out); file.seekg(40); const auto value=file.get();
          file.seekp(40); file.put(static_cast<char>(value ^ 1)); }
        rejects([&]{ cloud::readNoiseVolume(asset); }, "Changed payload must fail fingerprint verification");
        cloud::writeNoiseVolume(asset, volume);
        std::filesystem::resize_file(asset, std::filesystem::file_size(asset)-1);
        rejects([&]{ cloud::readNoiseVolume(asset); }, "Truncation must reject");
        cloud::writeNoiseVolume(asset, volume);
        { std::ofstream file(asset,std::ios::binary|std::ios::app); file.put('x'); }
        rejects([&]{ cloud::readNoiseVolume(asset); }, "Trailing data must reject");
        cloud::writeNoiseVolume(asset, volume);
        { std::fstream file(asset,std::ios::binary|std::ios::in|std::ios::out); file.seekp(8); file.put(2); }
        rejects([&]{ cloud::readNoiseVolume(asset); }, "Wrong generator/format version must reject");
        cloud::writeNoiseVolume(asset, volume);
        { std::fstream file(asset,std::ios::binary|std::ios::in|std::ios::out);
          file.seekp(12); for(int byte=0;byte<4;++byte)file.put(static_cast<char>(0xff)); }
        rejects([&]{ cloud::readNoiseVolume(asset); }, "Malformed dimensions must reject before allocation");
        cloud::writeNoiseVolume(asset, volume);
        { std::fstream file(asset,std::ios::binary|std::ios::in|std::ios::out); file.seekp(16); file.put(5); }
        rejects([&]{ cloud::readNoiseVolume(asset); }, "Period metadata must participate in identity");
        std::filesystem::remove_all(directory);
        std::cout << "Cloud noise asset contracts passed\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(directory); return 1;
    }
}
