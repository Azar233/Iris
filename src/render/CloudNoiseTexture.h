#pragma once
#include "optics/CloudNoiseVolume.h"

class Shader;

// All methods, including destruction, run on the GL context thread. CPU asset
// generation/validation can run on a worker before handing over an immutable volume.
class CloudNoiseTexture {
public:
    CloudNoiseTexture() = default;
    ~CloudNoiseTexture();
    CloudNoiseTexture(const CloudNoiseTexture&) = delete;
    CloudNoiseTexture& operator=(const CloudNoiseTexture&) = delete;
    void upload(const cloud::NoiseVolume& volume);
    void bindCanonical(Shader& shader, bool enabled, int period, unsigned int unit);
    unsigned int texture() const { return texture_; }
    std::size_t estimatedBytes() const { return bytes_ + (lightingTexture_ ? 129U * 64U * 4U : 0U); }
    const std::string& fingerprint() const { return fingerprint_; }
private:
    unsigned int texture_{0};
    unsigned int lightingTexture_{0};
    void ensureLightingTexture();
    std::size_t bytes_{0};
    std::string fingerprint_;
};
