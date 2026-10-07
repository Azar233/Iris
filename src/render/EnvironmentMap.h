#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <glm/vec3.hpp>

#include "optics/Atmosphere.h"

#include <glm/mat4x4.hpp>

class Shader;

// Decoded equirectangular HDR source. Kept on the map so the file environment can be restored
// after the analytic sky was previewed; the pixel buffer is large (a 4K map is ~100 MB), which
// is exactly why it is owned here instead of being copied into a lambda.
struct EquirectangularHdr {
    int width{0};
    int height{0};
    std::vector<float> pixels;

    bool valid() const {
        return width > 0 && height > 0
            && pixels.size() == static_cast<std::size_t>(width * height * 3);
    }
};

class EnvironmentMap {
public:
    EnvironmentMap(
        const std::filesystem::path& vertexShaderPath,
        const std::filesystem::path& fragmentShaderPath
    );
    ~EnvironmentMap();

    EnvironmentMap(const EnvironmentMap&) = delete;
    EnvironmentMap& operator=(const EnvironmentMap&) = delete;

    // Replaces the HDR environment with the sun-driven analytic sky. Rebuilds the radiance,
    // irradiance and prefiltered specular cubemaps. CPU rows are computed in parallel;
    // all GL work and publication remain on the calling (context-owning) thread.
    // The BRDF LUT is reused because it does not depend on the environment.
    //
    // `diffuseRadiance` feeds the irradiance pass only. The analytic sky passes a disk-free
    // lambda there: the direct sun is delivered by the analytic key light, and a 4.6e3-radiance
    // disk integrated by 128 uniform samples per texel would both double count it and firefly
    // (the disk is 5e-5 of the hemisphere, so a texel either misses it entirely or catches a
    // full-brightness sample). The prefiltered specular keeps the disk, which is the sun glint.
    //
    // Clouds and camera height do not enter these cloudless probes; the live cloud
    // layer resolves view-dependent parallax separately.
    void useAtmosphere(const atmosphere::AtmosphereParameters& parameters, bool separateWaterSun = false);
    // Opt-in water gets an additional disk-free specular probe; its direct solar
    // GGX term avoids sparse Monte Carlo hits on the tiny disk. Other materials
    // retain the original probe. HDR sources use the original probe for both.
    // Returns to the bundled HDR environment after an atmosphere preview.
    void useHdrSource(int preset = 0);
    // Wall time of the last cubemap rebuild, for the honest cost readout in the UI.
    double lastBuildMilliseconds() const { return lastBuildMilliseconds_; }

    void bind(unsigned int unit) const;
    void bindIrradiance(unsigned int unit) const;
    void bindPrefiltered(unsigned int unit) const;
    void bindWaterPrefiltered(unsigned int unit) const;
    void bindBrdfLut(unsigned int unit) const;
    void draw(const glm::mat4& inverseViewProjection, const glm::vec3& cameraPosition, float intensity) const;
    int maximumMipLevel() const { return maximumMipLevel_; }
    std::size_t estimatedBytes() const;

private:
    void build(
        const std::function<glm::vec3(const glm::vec3&)>& radiance,
        bool buildBrdfLut,
        const std::function<glm::vec3(const glm::vec3&)>& diffuseRadiance = {},
        const std::function<glm::vec3(const glm::vec3&)>& waterRadiance = {},
        const std::function<glm::vec3(const glm::vec3&)>& specularRadiance = {}
    );
    // Kept so the HDR environment can be restored after a sun-driven sky was previewed.
    EquirectangularHdr source_;
    EquirectangularHdr overcastSource_;
    EquirectangularHdr clearSource_;

    std::unique_ptr<Shader> shader_;
    unsigned int texture_{0};
    unsigned int irradianceTexture_{0};
    unsigned int prefilteredTexture_{0};
    unsigned int waterPrefilteredTexture_{0};
    bool analyticSun_{false};
    glm::vec3 visibleSunDirection_{0.0f, 1.0f, 0.0f};
    glm::vec3 visibleSunRadiance_{0.0f};
    unsigned int brdfLutTexture_{0};
    unsigned int vertexArray_{0};
    int maximumMipLevel_{0};
    int radianceFaceSize_{512};
    int prefilteredFaceSize_{64};
    double lastBuildMilliseconds_{0.0};
};
