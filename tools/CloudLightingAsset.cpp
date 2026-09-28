#include "optics/CloudLightingLut.h"
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error(
                "Usage: MyRendererCloudLightingAsset generate|verify file.cloudlut");
        cloud::LightingLut l;
        if (std::string(argv[1]) == "generate") {
            if (std::filesystem::exists(argv[2]))
                throw std::runtime_error("Refusing existing output");
            l = cloud::generateLightingLut();
            cloud::writeLightingLut(argv[2], l);
        } else if (std::string(argv[1]) == "verify")
            l = cloud::readLightingLut(argv[2]);
        else
            throw std::runtime_error("Unknown command");
        std::cout << l.fingerprint() << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
