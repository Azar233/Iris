#include "optics/CloudNoiseVolume.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
int integerArgument(const char* value) {
    std::size_t length=0;
    const int result=std::stoi(value,&length);
    if(length!=std::string(value).size()) throw std::runtime_error("Invalid integer argument");
    return result;
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::runtime_error("Usage: MyRendererCloudNoiseAsset generate <new.cloudnoise> <32|64> <period> | verify <file> | preview <file> <new.ppm>");
        const std::string mode = argv[1];
        const std::filesystem::path path = argv[2];
        if (mode == "generate" && argc == 5) {
            const auto manifest = std::filesystem::path(path.string() + ".json");
            if (std::filesystem::exists(path) || std::filesystem::exists(manifest))
                throw std::runtime_error("Refusing to overwrite cloud noise asset or manifest");
            const auto volume = cloud::generateNoiseVolume(integerArgument(argv[3]), integerArgument(argv[4]));
            cloud::writeNoiseVolume(path, volume);
            std::ofstream out(manifest);
            out << "{\n  \"format\": \"MyRendererCloudNoise\",\n  \"version\": 1,\n"
                << "  \"generator\": \"shared-worley3-perlin-fbm-v1\",\n"
                << "  \"resolution\": " << volume.resolution << ",\n  \"period\": " << volume.period
                << ",\n  \"encoding\": \"rgba16-unorm-little-endian\",\n"
                << "  \"channels\": [\"worley-body\", \"worley-secondary\", \"worley-detail\", \"perlin-fbm\"],\n"
                << "  \"fingerprintAlgorithm\": \"fnv1a64\",\n  \"fingerprint\": \"" << volume.fingerprint()
                << "\",\n  \"payloadBytes\": " << volume.texels.size() * 2 << "\n}\n";
            out.flush();
            if (!out) throw std::runtime_error("Cannot write cloud noise manifest");
            std::cout << volume.fingerprint() << '\n';
        } else if (mode == "verify" && argc == 3) {
            std::cout << cloud::readNoiseVolume(path).fingerprint() << '\n';
        } else if (mode == "preview" && argc == 4) {
            const auto volume = cloud::readNoiseVolume(path);
            const std::filesystem::path output = argv[3];
            if (std::filesystem::exists(output)) throw std::runtime_error("Refusing to overwrite preview");
            std::ofstream file(output, std::ios::binary);
            file << "P6\n" << volume.resolution * 4 << ' ' << volume.resolution << "\n255\n";
            for (int y = 0; y < volume.resolution; ++y) for (int channel = 0; channel < 4; ++channel)
                for (int x = 0; x < volume.resolution; ++x) {
                    const auto index = ((static_cast<std::size_t>(volume.resolution / 2) * volume.resolution + y)
                        * volume.resolution + x) * 4 + channel;
                    const char pixel = static_cast<char>(volume.texels[index] >> 8);
                    file.put(pixel); file.put(pixel); file.put(pixel);
                }
            file.flush(); if (!file) throw std::runtime_error("Cannot write cloud noise preview");
        } else throw std::runtime_error("Invalid cloud noise asset command");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
