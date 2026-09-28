#include "asset/InputManifest.h"
#include "optics/CloudLightingLut.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool b, const char* m) {
    if (!b)
        throw std::runtime_error(m);
}
} // namespace
int main() {
    try {
        const auto root = std::filesystem::current_path() / "cloud-capture-input-test";
        std::filesystem::create_directories(root);
        auto lut = cloud::generateLightingLut();
        const auto path = root / "transport.cloudlut";
        cloud::writeLightingLut(path, lut);
        require(cloud::readLightingLut(path).fingerprint() == cloud::canonicalLightingFingerprint,
                "Lighting generator identity drift");
        float worst = 0;
        for (int i = 0; i <= 100000; ++i) {
            const float depth = float(i) * 40.0f / 100000.0f;
            worst = std::max(worst, std::abs(lut.sample(depth) - std::exp(-depth)));
        }
        require(worst < 2.1e-6f, "Lighting LUT approximation regression");
        require(lut.sample(0) == 1 && lut.sample(32) >= 0 && lut.sample(80) > 0,
                "LUT endpoints and deep tail");
        auto corrupt = path;
        {
            std::fstream file(corrupt, std::ios::in | std::ios::out | std::ios::binary);
            file.seekp(100);
            file.put('x');
        }
        bool rejected = false;
        try {
            cloud::readLightingLut(corrupt);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Corrupt LUT accepted");
        cloud::writeLightingLut(path, lut);
        {
            std::ofstream f(path, std::ios::binary | std::ios::app);
            f.put('x');
        }
        rejected = false;
        try {
            cloud::readLightingLut(path);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Trailing LUT bytes accepted");
        const auto input = root / "mutable.bin";
        const auto missing = root / "fallback.png";
        std::filesystem::remove(missing);
        {
            std::ofstream f(input);
            f << "abcd";
        }
        {
            capture::InputManifest manifest;
            manifest.record(input);
            manifest.record(input);
            manifest.record(missing);
            manifest.validate();
            require(manifest.files.size() == 2, "Manifest must deduplicate");
            const auto time = std::filesystem::last_write_time(input);
            {
                std::ofstream f(input);
                f << "abce";
            }
            std::filesystem::last_write_time(input, time);
            rejected = false;
            try {
                manifest.validate();
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Same size/timestamp edit undetected");
        }
        {
            capture::InputManifest manifest;
            manifest.record(missing);
            {
                std::ofstream f(missing);
                f << "created";
            }
            rejected = false;
            try {
                manifest.validate();
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Missing fallback becoming present undetected");
        }
        {
            capture::InputManifest manifest;
            manifest.record(input);
            std::filesystem::remove(input);
            rejected = false;
            try {
                manifest.validate();
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Deleted dependency undetected");
        }
        require(capture::activeManifest == nullptr, "Capture session must reset");
        std::cout << "Capture inputs and lighting LUT passed, max absolute error=" << worst << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
