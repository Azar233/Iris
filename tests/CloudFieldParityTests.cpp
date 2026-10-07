// GPU / CPU parity for the shared cloud density field (P1-A slice 6, step C2).
//
// The design premise of the cloud work is that one source file -- `src/optics/CloudField.h` -- is
// compiled by both the C++ compiler and the GPU driver, so a traced cloud and a rendered cloud
// cannot disagree about the model. That premise rests on an assumption worth measuring rather than
// assuming: GLSL only guarantees 16 bits of precision for `highp uint` in the specification, while
// the field is built on 32-bit integer hashing, and the two sides also differ in `floor`, division
// and constant folding.
//
// This test renders `shaders/cloud_field_test.frag` over a grid of sample points and compares every
// output against the same function evaluated on the CPU, value by value, so a disagreement names the
// step it came from instead of only reporting that something differs.
//
// Findings over 4096 samples, all of them agreement to float rounding:
//   - `hashUnit` is **bit-identical**: the 32-bit integer hash survives the GPU driver intact, so
//     both sides evaluate one field and the `highp uint` precision worry is settled for this
//     hardware. This is the finding the whole shared-source approach depends on.
//   - `worley` 2.4e-7, `baseShape` 2.1e-7, `detailShape` 2.4e-7, `taper` 6.0e-8, `profile` 2.4e-7
//     absolute.
//   - the three weather channels to the same order, and `density` to a few parts per million. The
//     arithmetic between the field and the density became a good deal longer in C5 -- a weather
//     sample, a coverage remap and an erosion step in front of the threshold -- which is exactly why
//     each of those steps reports its own channel instead of only the final value.
//
// The phase function used to be probed here too. It moved out when C5 needed the third attachment
// for the base/detail split, and it is not unmeasured: `cloud-march-parity` compares a full GPU march
// against the CPU reference's radiance, which carries the phase through both the single-scatter lobe
// and every multiple-scatter octave. A phase disagreement cannot hide behind that comparison -- it
// simply changes the radiance -- even though it is a looser check than a direct probe was.
//
// What this test cost is worth recording, because the cost was not in the field. Two revisions
// chased a one-unit density disagreement that turned out to be the harness's own fault: it uploaded
// a wind vector with `setVec3` into a `vec2` uniform, and `glUniform2fv` read the first two floats of
// a three-float value the caller never meant as a pair. `Shader::setVec2` now exists so that mistake
// is not available, and the position, taper and profile probes used to chase it are gone -- the
// shader is once again a pure function of its declared inputs.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "optics/CloudFieldCpp.h"
#include "render/Shader.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr int gridWidth = 64;
constexpr int gridHeight = 64;

// The sample grid. The field is sampled over more than one tile period so the wrap is exercised, and
// the density over a range of heights so the profile is swept too. The weather map has its own,
// much wider sweep in weather tiles: one cloud tile is a quarter of a weather tile at the shipped
// scales, so a grid sized for the cloud field would never leave a single weather cell.
constexpr float tileOriginX = -1.5f;
constexpr float tileOriginY = 0.25f;
constexpr float tileSpan = 7.0f;
constexpr float weatherOriginX = -2.25f;
constexpr float weatherOriginY = 1.75f;
constexpr float weatherSpan = 9.0f;
constexpr float worldOriginX = -4000.0f;
constexpr float worldOriginZ = 2500.0f;
constexpr float worldSpan = 9000.0f;
constexpr int period = 4;
constexpr float baseHeight = 3200.0f;
constexpr float topHeight = 8000.0f;
constexpr float featureScale = 6400.0f;
constexpr float weatherScale = 25600.0f;
constexpr float windX = 137.0f;
constexpr float windZ = -88.0f;
constexpr float coverage = 0.50f;
constexpr float densityScale = 1.0f;
constexpr float coverageVariation = 0.90f;
constexpr float cloudType = 0.65f;
constexpr float typeVariation = 0.50f;
constexpr float heightVariation = 0.30f;
constexpr float detailStrength = 0.45f;
constexpr float detailEdge = 0.15f;
float shapeBlend = 0.0f;

// The layer struct is built the same way on both sides. `cloud::makeCloudParams` is the CPU half;
// this is the same field-by-field assignment the shader performs from its uniforms, kept here rather
// than inline in `cpuSample` so the two sides can be diffed by eye.
MyRendererCloudParams cpuLayer() {
    MyRendererCloudParams layer{};
    layer.baseHeight = baseHeight;
    layer.topHeight = topHeight;
    layer.featureScale = featureScale;
    layer.noisePeriod = period;
    layer.windX = windX;
    layer.windZ = windZ;
    layer.coverage = coverage;
    layer.densityScale = densityScale;
    layer.weatherScale = weatherScale;
    layer.coverageVariation = coverageVariation;
    layer.cloudType = cloudType;
    layer.typeVariation = typeVariation;
    layer.heightVariation = heightVariation;
    layer.detailStrength = detailStrength;
    layer.detailEdge = detailEdge;
    layer.shapeBlend = shapeBlend;
    return layer;
}

struct Sample {
    // outField: (hashUnit, worley, baseShape, detailShape)
    glm::vec4 field{0.0f};
    // outWeather: (red, green, blue, profile)
    glm::vec4 weather{0.0f};
    // outDensity: (taper, localCoverage, density, baseCloud)
    glm::vec4 density{0.0f};
};

// The CPU evaluation of the shader's body, with the same input mapping. Kept textually adjacent to
// the shader on purpose: if either side's mapping changes, the other has to change with it.
Sample cpuSample(float u, float v) {
    Sample sample;
    const MyRendererCloudParams layer = cpuLayer();
    const float tileX = tileOriginX + u * tileSpan;
    const float tileY = tileOriginY + v * tileSpan;
    sample.field.x = myrenderer_cloud_hash_unit(
        static_cast<int>(std::floor(tileX * 64.0f)),
        static_cast<int>(std::floor(tileY * 64.0f)));
    sample.field.y = myrenderer_cloud_worley(tileX, tileY, period);
    sample.field.z = myrenderer_cloud_base_shape(tileX, tileY, period);
    sample.field.w = myrenderer_cloud_detail_shape(tileX, tileY, period);

    const float weatherX = weatherOriginX + u * weatherSpan;
    const float weatherY = weatherOriginY + v * weatherSpan;
    const float worldX = worldOriginX + u * worldSpan;
    const float worldZ = worldOriginZ + v * worldSpan;
    const float worldY = baseHeight + v * (topHeight - baseHeight);
    const float slab = std::max(topHeight - baseHeight, 1.0e-3f);
    const float taper = (worldY - baseHeight) / slab;
    const float profile = myrenderer_cloud_layer_profile(worldX, worldY, worldZ, layer);
    sample.weather.x = myrenderer_cloud_weather(weatherX, weatherY, 0);
    sample.weather.y = myrenderer_cloud_weather(weatherX, weatherY, 1);
    sample.weather.z = myrenderer_cloud_weather(weatherX, weatherY, 2);
    sample.weather.w = profile;

    const float densityWeatherX = (worldX + windX) / weatherScale;
    const float densityWeatherY = (worldZ + windZ) / weatherScale;
    const float localCoverage = std::clamp(
        coverage + (myrenderer_cloud_weather(densityWeatherX, densityWeatherY, 0) - 0.5f)
            * coverageVariation, 0.0f, 1.0f);
    sample.density.x = taper;
    sample.density.y = localCoverage;
    sample.density.z = myrenderer_cloud_density(worldX, worldY, worldZ, layer);
    const float warpedTileX = (worldX + windX) / featureScale
        + myrenderer_cloud_height_warp_x(taper) * 0.15f;
    const float warpedTileY = (worldZ + windZ) / featureScale
        + myrenderer_cloud_height_warp_y(taper) * 0.15f;
    sample.density.w = localCoverage > 0.0f
        ? std::clamp((myrenderer_cloud_volume_base_shape(
            warpedTileX, warpedTileY, taper, period) * profile
            - (1.0f - localCoverage)) / localCoverage, 0.0f, 1.0f)
        : 0.0f;
    return sample;
}

// Difference in units of the last place, which is what distinguishes "bit identical" from "close".
int ulpDifference(float a, float b) {
    if (a == b) return 0;
    if (std::isnan(a) || std::isnan(b)) return 1 << 30;
    std::int32_t ia = 0;
    std::int32_t ib = 0;
    std::memcpy(&ia, &a, sizeof(ia));
    std::memcpy(&ib, &b, sizeof(ib));
    // Map the float bit patterns onto a monotone integer ordering so a comparison across zero works.
    const auto ordered = [](std::int32_t value) {
        return value < 0 ? static_cast<std::int64_t>(0x80000000u) - value
                         : static_cast<std::int64_t>(value);
    };
    const std::int64_t difference = ordered(ia) - ordered(ib);
    return static_cast<int>(difference < 0 ? -difference : difference);
}

struct Comparison {
    int worstUlp{0};
    float worstAbsolute{0.0f};
    std::size_t mismatched{0U};
    std::size_t total{0U};
    std::string worstSource;
};

void compareChannel(const char* name, float gpu, float cpu, Comparison& comparison) {
    ++comparison.total;
    if (std::isnan(gpu) || std::isnan(cpu)) {
        throw std::runtime_error(std::string("NaN in ") + name);
    }
    const int ulp = ulpDifference(gpu, cpu);
    if (ulp != 0) ++comparison.mismatched;
    if (ulp > comparison.worstUlp) {
        comparison.worstUlp = ulp;
        comparison.worstSource = name;
    }
    comparison.worstAbsolute = std::max(comparison.worstAbsolute, std::abs(gpu - cpu));
}

std::vector<float> readTexture(unsigned int texture, int width, int height) {
    std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4U);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
    return pixels;
}

} // namespace

int main(int argc, char** argv) {
    shapeBlend = argc >= 2 && std::string(argv[1]) == "--shape" ? 0.85f : 0.0f;
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "GLFW initialization failed\n";
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(32, 32, "Cloud field parity", nullptr, nullptr);
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
        // The shader path carries the repository root as its parent so the shared header's
        // `src/optics/CloudField.h` include resolves, which is the same rule the build uses for
        // assets.
        const std::filesystem::path fragmentPath =
            sourceRoot / "shaders" / "cloud_field_test.frag";
        Shader shader(sourceRoot / "shaders" / "fullscreen.vert", fragmentPath);
        shader.use();
        shader.setVec3("uTile", glm::vec3(tileOriginX, tileOriginY, tileSpan));
        shader.setVec3("uWeatherTile", glm::vec3(weatherOriginX, weatherOriginY, weatherSpan));
        shader.setInt("uPeriod", period);
        shader.setVec3("uWorld", glm::vec3(worldOriginX, worldOriginZ, worldSpan));
        shader.setFloat("uBaseHeight", baseHeight);
        shader.setFloat("uTopHeight", topHeight);
        shader.setFloat("uFeatureScale", featureScale);
        shader.setInt("uNoisePeriod", period);
        shader.setFloat("uWeatherScale", weatherScale);
        shader.setVec2("uWind", glm::vec2(windX, windZ));
        shader.setFloat("uCoverage", coverage);
        shader.setFloat("uDensityScale", densityScale);
        shader.setFloat("uCoverageVariation", coverageVariation);
        shader.setFloat("uCloudType", cloudType);
        shader.setFloat("uTypeVariation", typeVariation);
        shader.setFloat("uHeightVariation", heightVariation);
        shader.setFloat("uDetailStrength", detailStrength);
        shader.setFloat("uDetailEdge", detailEdge);
        shader.setFloat("uShapeBlend", shapeBlend);

        // Three float attachments, so the comparison reads the shader's exact output rather than a
        // display-encoded version of it.
        unsigned int textures[3] = {0U, 0U, 0U};
        glGenTextures(3, textures);
        for (unsigned int texture : textures) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, gridWidth, gridHeight, 0, GL_RGBA,
                GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        unsigned int framebuffer = 0U;
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[0], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, textures[1], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, textures[2], 0);
        const GLenum attachments[3] = {
            GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
        glDrawBuffers(3, attachments);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Parity framebuffer is incomplete");
        }
        glViewport(0, 0, gridWidth, gridHeight);
        glDisable(GL_DEPTH_TEST);
        // A Core Profile context requires a bound vertex array object even when the vertex shader
        // generates its positions from `gl_VertexID` and reads no attributes. Without one the draw is
        // rejected with GL_INVALID_OPERATION on some drivers and silently accepted on others.
        unsigned int vertexArray = 0U;
        glGenVertexArrays(1, &vertexArray);
        glBindVertexArray(vertexArray);
        while (glGetError() != GL_NO_ERROR) {
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
        const GLenum drawError = glGetError();
        if (drawError != GL_NO_ERROR) {
            throw std::runtime_error("Parity draw failed with GL error "
                + std::to_string(drawError));
        }

        const std::vector<float> fieldPixels = readTexture(textures[0], gridWidth, gridHeight);
        const std::vector<float> weatherPixels = readTexture(textures[1], gridWidth, gridHeight);
        const std::vector<float> densityPixels = readTexture(textures[2], gridWidth, gridHeight);

        Comparison hash;
        Comparison worley;
        Comparison baseShape;
        Comparison detailShape;
        Comparison weatherRed;
        Comparison weatherGreen;
        Comparison weatherBlue;
        Comparison profile;
        Comparison taper;
        Comparison localCoverage;
        Comparison baseCloud;
        Comparison density;

        for (int y = 0; y < gridHeight; ++y) {
            for (int x = 0; x < gridWidth; ++x) {
                // The fragment centre, which is what the rasteriser interpolated at.
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(gridWidth);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(gridHeight);
                const Sample cpu = cpuSample(u, v);
                // `glGetTexImage` returns the top row first, which is the same order this loop
                // walks, so row index is row index and no flip is involved.
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(gridWidth)
                        + static_cast<std::size_t>(x)) * 4U;
                compareChannel("hashUnit", fieldPixels[offset + 0U], cpu.field.x, hash);
                compareChannel("worley", fieldPixels[offset + 1U], cpu.field.y, worley);
                compareChannel("baseShape", fieldPixels[offset + 2U], cpu.field.z, baseShape);
                compareChannel("detailShape", fieldPixels[offset + 3U], cpu.field.w, detailShape);
                compareChannel("weatherRed", weatherPixels[offset + 0U], cpu.weather.x, weatherRed);
                compareChannel("weatherGreen", weatherPixels[offset + 1U], cpu.weather.y, weatherGreen);
                compareChannel("weatherBlue", weatherPixels[offset + 2U], cpu.weather.z, weatherBlue);
                compareChannel("profile", weatherPixels[offset + 3U], cpu.weather.w, profile);
                // The density is checked from its inputs inward. A wrong sample position, a wrong
                // weather sample, a wrong field or a wrong height profile all surface as a wrong
                // density, and only checking them separately says which one it was.
                compareChannel("taper", densityPixels[offset + 0U], cpu.density.x, taper);
                compareChannel("localCoverage", densityPixels[offset + 1U], cpu.density.y,
                    localCoverage);
                compareChannel("baseCloud", densityPixels[offset + 3U], cpu.density.w, baseCloud);
                compareChannel("density", densityPixels[offset + 2U], cpu.density.z, density);
            }
        }

        const auto report = [](const char* name, const Comparison& comparison) {
            std::cout << "  " << name << ": worst " << comparison.worstUlp << " ULP, max abs "
                << comparison.worstAbsolute << ", mismatched " << comparison.mismatched << "/"
                << comparison.total << '\n';
        };
        std::cout << "Cloud field GPU/CPU parity over " << gridWidth << "x" << gridHeight
            << " samples:\n";
        report("hashUnit", hash);
        report("worley", worley);
        report("baseShape", baseShape);
        report("detailShape", detailShape);
        report("weatherRed", weatherRed);
        report("weatherGreen", weatherGreen);
        report("weatherBlue", weatherBlue);
        report("profile", profile);
        report("taper", taper);
        report("localCoverage", localCoverage);
        report("baseCloud", baseCloud);
        report("density", density);

        // The contract. Absolute error is the criterion, not relative: a relative bound is
        // meaningless for a value that is legitimately near zero, and `density` is zero across much
        // of the grid.
        require(hash.worstUlp == 0, "the integer hash must be bit-identical on the GPU");
        require(worley.worstAbsolute < 1.0e-5f, "worley must agree in absolute terms");
        require(baseShape.worstAbsolute < 1.0e-5f, "the base shape must agree in absolute terms");
        require(detailShape.worstAbsolute < 1.0e-5f, "the detail shape must agree in absolute terms");
        // The weather channels ride on the same salted hash as everything else, so they are held to
        // the same bound rather than a looser one.
        require(weatherRed.worstAbsolute < 1.0e-5f, "the weather map's coverage must agree");
        require(weatherGreen.worstAbsolute < 1.0e-5f, "the weather map's cloud type must agree");
        require(weatherBlue.worstAbsolute < 1.0e-5f, "the weather map's height must agree");
        require(profile.worstAbsolute < 1.0e-6f, "the height profile must agree");
        require(taper.worstAbsolute < 1.0e-6f, "the taper must agree");
        require(localCoverage.worstAbsolute < 1.0e-6f, "the local coverage must agree");
        require(baseCloud.worstAbsolute < 1.0e-5f, "the thresholded base cloud must agree");
        // The value every consumer reads. Its bound is looser than the field's own because the
        // coverage step divides by `coverage`, which amplifies the field's error; a hundredth of a
        // percent is still far below anything a volumetric comparison needs.
        require(density.worstAbsolute < 1.0e-4f, "density must agree to a hundredth of a percent");

        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(3, textures);
        glDeleteVertexArrays(1, &vertexArray);
    } catch (const std::exception& error) {
        std::cerr << "Cloud field parity failed: " << error.what() << '\n';
        result = 1;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
