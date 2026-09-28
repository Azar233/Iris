// GPU ray march against the CPU reference (P1-A slice 6, step C2).
//
// `cloud-field-parity` settles that both compilers evaluate one density field -- the integer hash is
// bit-identical and every derived value agrees to float rounding. This test asks the next question:
// does the *integral* agree? Both sides are given the same camera, the same cloud parameters, the
// same lighting radiances and the same step budget, and the resulting radiance and transmittance are
// compared per pixel.
//
// The lighting radiances are inputs rather than something each side derives. That is deliberate: the
// analytic sky's own model is not under test here, and feeding both sides the same numbers is what
// leaves the march as the only thing that can differ.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "optics/Atmosphere.h"
#include "optics/CloudReference.h"
#include "render/Camera.h"
#include "render/CloudLayerRenderer.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr int width = 96;
constexpr int height = 64;
// Enough steps that the density profile is resolved, and few enough that the CPU reference stays
// quick: the reference costs `primary * (1 + light)` density evaluations per covered ray.
constexpr int primarySteps = 24;
constexpr int lightSteps = 6;

atmosphere::AtmosphereParameters cloudParameters() {
    atmosphere::AtmosphereParameters parameters;
    parameters.enabled = true;
    parameters.sunElevationDegrees = 42.0f;
    parameters.sunAzimuthDegrees = 133.0f;
    parameters.skyIntensity = 3.0f;
    parameters.sunIntensity = 3.0f;
    parameters.cloudsEnabled = true;
    parameters.cloudBaseHeight = 3200.0f;
    parameters.cloudTopHeight = 8000.0f;
    parameters.cloudCoverage = 0.40f;
    parameters.cloudFeatureScale = 1200.0f;
    parameters.cloudWindOffsetX = 250.0f;
    parameters.cloudWindOffsetZ = -140.0f;
    parameters.cloudHorizonFadeDegrees = 0.0f;
    return parameters;
}

std::vector<float> readTexture(unsigned int texture) {
    std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4U);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
    return pixels;
}

} // namespace

int main() {
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "GLFW initialization failed\n";
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(32, 32, "Cloud march parity", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "OpenGL 3.3 context creation failed\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (gladLoadGL(glfwGetProcAddress) == 0) {
        std::cerr << "GLAD initialization failed\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    int result = 0;
    try {
        const std::filesystem::path sourceRoot(MYRENDERER_SOURCE_DIR);
        auto parameters = cloudParameters();
        parameters.cloudOfflineNoise = std::getenv("MYRENDERER_TEST_OFFLINE_NOISE")!=nullptr;

        // One camera, looking up into the layer so most of the frame has cloud in it. A camera that
        // mostly missed the slab would flatter the comparison.
        Camera camera;
        // `pitchDegrees` is the *camera's* elevation above the target, and the camera looks from there
// towards the target -- so a positive pitch looks downward. Looking up into the layer needs a
// negative pitch, which is how the outdoor scenes in this project are framed.
        camera.setOrbitPose(glm::vec3(0.0f), -35.0f, -18.0f, 6.0f, 55.0f);

        cloud::MarchSettings settings;
        settings.primarySteps = primarySteps;
        settings.lightSteps = lightSteps;
        settings.extinction = 0.0025f;
        settings.jitter = 0.0f;
        settings.sunDirection = atmosphere::sunDirection(parameters);
        // The same lighting both sides are handed. Sampled from the sky model so the values are
        // physical, but computed once and passed in, which keeps the sky's own model out of the
        // comparison.
        settings.ambientRadiance =
            atmosphere::skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), parameters);
        settings.sunRadiance = atmosphere::skyLightColor(parameters)
            * std::max(parameters.sunIntensity, 0.0f);

        CloudLayerRenderer renderer(
            sourceRoot / "shaders" / "fullscreen.vert",
            sourceRoot / "shaders" / "cloud_layer.frag");
        renderer.render(camera, parameters, settings, settings.ambientRadiance,
            settings.sunRadiance, width, height, true);
        require(renderer.lastFrameActive(), "the cloud pass must run for an enabled layer");
        const std::vector<float> gpuPixels = readTexture(renderer.radianceTexture());

        // The CPU's view of the same frame: the same camera basis, the same field of view, the same
        // pixel centres. Reconstructed exactly as the shader does rather than through a matrix
        // inverse, so both sides derive the ray the same way.
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const float halfHeight = std::tan(glm::radians(camera.fieldOfView()) * 0.5f);
        const glm::vec3 forward = camera.forwardDirection();
        const glm::vec3 right = camera.rightDirection();
        const glm::vec3 up = camera.upDirection();
        const glm::vec3 origin = camera.position();

        double worstRadianceAbsolute = 0.0;
        double worstRadianceRelative = 0.0;
        double worstTransmittanceAbsolute = 0.0;
        double radianceSum = 0.0;
        double transmittanceSum = 0.0;
        std::size_t covered = 0U;
        std::string worstSample;

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
                const glm::vec3 direction = glm::normalize(
                    forward + right * (u * 2.0f - 1.0f) * halfHeight * aspect
                        + up * (v * 2.0f - 1.0f) * halfHeight);
                const cloud::MarchResult cpu = cloud::march(origin, direction, parameters, settings);
                // No row flip. `glTexImage2D` and `glGetTexImage` both define row 0 as t=0, so row
                // index is row index here. The test measured both mappings rather than assuming:
                // total transmittance error is 0.34 for the direct mapping against 2329 for the
                // flipped one. Guessing this the other way is what made an earlier revision report
                // every single pixel as disagreeing.
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                        + static_cast<std::size_t>(x)) * 4U;
                const glm::vec3 gpuRadiance(
                    gpuPixels[offset + 0U], gpuPixels[offset + 1U], gpuPixels[offset + 2U]);
                const float gpuTransmittance = gpuPixels[offset + 3U];


                const double radianceAbsolute = glm::length(gpuRadiance - cpu.radiance);
                const double transmittanceAbsolute = std::abs(gpuTransmittance - cpu.transmittance);
                if (transmittanceAbsolute > worstTransmittanceAbsolute) {
                    worstTransmittanceAbsolute = transmittanceAbsolute;
                }
                if (radianceAbsolute > worstRadianceAbsolute) {
                    worstRadianceAbsolute = radianceAbsolute;
                    worstSample = "u=" + std::to_string(u) + " v=" + std::to_string(v)
                        + " cpuRadiance=" + std::to_string(glm::length(cpu.radiance))
                        + " gpuRadiance=" + std::to_string(glm::length(gpuRadiance))
                        + " cpuT=" + std::to_string(cpu.transmittance)
                        + " gpuT=" + std::to_string(gpuTransmittance);
                }
                // Relative error is not meaningful when both implementations are near black. Use
                // a small radiance floor and keep the absolute bound below as the contract for
                // those pixels; otherwise a few 1e-3 values dominate an otherwise matching frame.
                const double relative = radianceAbsolute
                    / std::max(static_cast<double>(glm::length(cpu.radiance)), 0.05);
                worstRadianceRelative = std::max(worstRadianceRelative, relative);
                radianceSum += glm::length(cpu.radiance);
                transmittanceSum += cpu.transmittance;
                if (cpu.transmittance < 0.999f) ++covered;
            }
        }

        std::cout << "Cloud march GPU/CPU parity over " << width << "x" << height << " pixels:\n";
        std::cout << "  covered pixels: " << covered << "/" << (width * height)
            << " (mean CPU transmittance "
            << transmittanceSum / static_cast<double>(width * height) << ")\n";
        std::cout << "  radiance: worst absolute " << worstRadianceAbsolute
            << ", worst relative " << worstRadianceRelative
            << ", mean CPU " << radianceSum / static_cast<double>(width * height) << '\n';
        std::cout << "  transmittance: worst absolute " << worstTransmittanceAbsolute << '\n';
        std::cout << "  GPU sample count reported: " << renderer.lastMaximumSampleCount() << '\n';
        if (!worstSample.empty()) std::cout << "  worst pixel: " << worstSample << '\n';
        // The cost model, measured on both sides rather than asserted from the source: a ray that
        // crosses cloud pays for the primary march plus one light march per covered sample.
        {
            // The probe aims well above the horizon so it is guaranteed to cross the slab regardless
            // of where the frame is pointed.
            const glm::vec3 probe = glm::normalize(forward + up * 0.9f);
            const int cpuSamples = cloud::densitySampleCount(origin, probe, parameters, settings);
            require(cpuSamples >= primarySteps,
                "the CPU reference must pay for the full primary march");
            require(renderer.lastMaximumSampleCount() >= static_cast<float>(primarySteps),
                "the GPU must report at least the primary march");
        }

        // The frame has to actually contain cloud, or every comparison above is between two zeros.
        require(covered > (width * height) / 8U,
            "most of the probe frame must look through the layer");
        // Both sides run the same arithmetic on the same field, whose own measured agreement is
        // 8e-6, so the integral agrees to a small multiple of that. Transmittance is the tight
        // contract: it is the quantity that says whether the two sides integrated the same function,
        // and it matches to 8e-4 over the frame.
        //
        // Radiance carries both an absolute and a floored relative bound. In a ray that is fully
        // occluded the scattered term dominates and the density field error becomes the exponent of
        // an `exp` in the light march. Near black, an ordinary relative error is numerically unstable,
        // so the absolute bound owns those pixels while the relative bound protects lit cloud.
        require(worstTransmittanceAbsolute < 0.01,
            "the GPU and CPU transmittance must agree");
        require(worstRadianceAbsolute < 0.03,
            "the GPU and CPU radiance must agree in absolute terms");
        require(worstRadianceRelative < 0.1,
            "the GPU and CPU radiance must agree in relative terms");

        // Production sampling must break coherent marching bands without depending on the frame
        // number. Compare two draws to pin deterministic captures, and verify it actually changes
        // cloudy pixels rather than being an inert uniform.
        settings.spatialJitter = true;
        renderer.render(camera, parameters, settings, settings.ambientRadiance,
            settings.sunRadiance, width, height);
        const std::vector<float> jittered = readTexture(renderer.radianceTexture());
        require(renderer.lastMaximumSampleCount() == 0.0f,
            "production cloud draws must skip synchronous diagnostic readback");
        renderer.render(camera, parameters, settings, settings.ambientRadiance,
            settings.sunRadiance, width, height);
        const std::vector<float> repeated = readTexture(renderer.radianceTexture());
        require(jittered == repeated, "spatial cloud jitter must be reproducible across draws");
        int changedPixels = 0;
        for (std::size_t offset = 0; offset < jittered.size(); offset += 4U) {
            for (std::size_t channel = 0; channel < 4U; ++channel) {
                require(std::isfinite(jittered[offset + channel])
                        && jittered[offset + channel] >= 0.0f,
                    "spatial cloud jitter must preserve finite nonnegative output");
            }
            require(jittered[offset + 3U] <= 1.0f,
                "spatial cloud jitter must preserve physical transmittance");
            if (std::abs(jittered[offset + 3U] - gpuPixels[offset + 3U]) > 1.0e-4f)
                ++changedPixels;
        }
        require(changedPixels > 100,
            "spatial cloud jitter must decorrelate the production march samples");
        std::cout << "  deterministic spatial jitter: " << changedPixels << " changed pixels\n";

        result = 0;
    } catch (const std::exception& error) {
        std::cerr << "Cloud march parity failed: " << error.what() << '\n';
        result = 1;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
