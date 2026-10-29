#include "vulkan/VulkanBootstrap.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        bool validation = false;
        bool synchronization = false;
        std::filesystem::path output = "vulkan-smoke";
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--require-validation") validation = true;
            else if (argument == "--require-sync-validation") { validation = true; synchronization = true; }
            else if (argument == "--output" && i + 1 < argc) output = argv[++i];
            else throw std::runtime_error("Usage: IrisVulkanSmoke [--require-validation] [--require-sync-validation] [--output directory]");
        }
        // Repeat the complete create/render/resize/destroy lifetime.
        const auto first = iris::vulkan::runBootstrap(validation, synchronization);
        const auto result = iris::vulkan::runBootstrap(validation, synchronization);
        if (first.rgba != result.rgba || result.renderedFrames != 9 || result.swapchainCreations < 3
            || result.width != 400 || result.height != 240)
            throw std::runtime_error("Vulkan repeated lifetime/frame/resize checks failed");
        const auto encoded = [](double linear) { return static_cast<int>(std::round(255.0 * (1.055 * std::pow(linear, 1.0 / 2.4) - 0.055))); };
        const int expected[]{encoded(0.125), encoded(0.25), encoded(0.5), 255};
        for (std::size_t i = 0; i < result.rgba.size(); ++i)
            if (std::abs(static_cast<int>(result.rgba[i]) - expected[i % 4]) > 1)
                throw std::runtime_error("Dynamic Rendering/readback clear pixels differ from expected sRGB values");
        std::filesystem::create_directories(output);
        if (!stbi_write_png((output / "clear.png").string().c_str(), static_cast<int>(result.width), static_cast<int>(result.height),
            4, result.rgba.data(), static_cast<int>(result.width * 4))) throw std::runtime_error("Cannot write Vulkan screenshot");
        std::ofstream report(output / "report.json");
        report << "{\n  \"device\": " << std::quoted(result.deviceName) << ",\n  \"apiVersion\": " << result.apiVersion
            << ",\n  \"validationLayerVersion\": " << result.validationLayerVersion
            << ",\n  \"validationEnabled\": " << (result.validationEnabled ? "true" : "false")
            << ",\n  \"synchronizationValidation\": " << (result.synchronizationValidation ? "true" : "false")
            << ",\n  \"validationWarnings\": " << result.validationWarnings << ",\n  \"validationErrors\": " << result.validationErrors
            << ",\n  \"framesPerRun\": " << result.renderedFrames << ",\n  \"lifetimeRuns\": 2,\n  \"swapchainCreationsPerRun\": " << result.swapchainCreations
            << ",\n  \"width\": " << result.width << ",\n  \"height\": " << result.height << "\n}\n";
        report.close(); if (!report) throw std::runtime_error("Cannot write Vulkan report");
        std::cout << "Vulkan Dynamic Rendering/2 frames in flight/resize/readback/repeated lifetime: PASS\n"
            << "Device: " << result.deviceName << "\nValidation: " << (result.validationEnabled ? "enabled" : "unavailable (not verified)")
            << "\nSynchronization validation: " << (result.synchronizationValidation ? "enabled" : "unavailable (not verified)") << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
