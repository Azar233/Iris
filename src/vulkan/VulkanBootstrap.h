#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace iris::vulkan {
struct BootstrapReport {
    std::string deviceName;
    std::uint32_t apiVersion{0};
    std::uint32_t validationLayerVersion{0};
    bool validationEnabled{false}, synchronizationValidation{false};
    unsigned validationWarnings{0}, validationErrors{0};
    unsigned renderedFrames{0}, swapchainCreations{0};
    unsigned width{0}, height{0};
    std::vector<unsigned char> rgba;
};

// M3-A1 diagnostic only: owns its GLFW lifetime and a no-OpenGL window on
// the calling thread. This is not yet a Scene/Job renderer or a general RHI.
BootstrapReport runBootstrap(bool requireValidation, bool requireSynchronization = false);
}
