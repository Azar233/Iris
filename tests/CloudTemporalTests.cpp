#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "render/Camera.h"
#include "render/CloudLayerRenderer.h"

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<float> read(unsigned int texture, int width, int height) {
    std::vector<float> result(static_cast<std::size_t>(width) * height * 4);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, result.data());
    for (float value : result) require(std::isfinite(value), "cloud buffers must stay finite");
    return result;
}
double alphaError(const std::vector<float>& a, const std::vector<float>& b) {
    double error = 0;
    for (std::size_t i = 3; i < a.size(); i += 4) error += std::abs(a[i] - b[i]);
    return error / static_cast<double>(a.size() / 4);
}
void save(const std::filesystem::path& path, const std::vector<float>& pixels, int width, int height) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) for (int x = 0; x < width; ++x) {
        for (int c = 0; c < 3; ++c) {
            const float value = std::max(pixels[(static_cast<std::size_t>(y) * width + x) * 4 + c], 0.0f);
            const auto byte = static_cast<unsigned char>(std::pow(value / (1.0f + value), 1.0f / 2.2f) * 255.0f);
            file.write(reinterpret_cast<const char*>(&byte), 1);
        }
    }
    require(static_cast<bool>(file), "could not write cloud acceptance image");
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& output,
    atmosphere::CloudQualityTier tier) {
    constexpr int width = 192, height = 128;
    atmosphere::AtmosphereParameters parameters;
    parameters.enabled = true;
    atmosphere::applyCloudPreset(parameters, atmosphere::CloudPreset::Cumulus);
    parameters.cloudHalfResolution = true;
    parameters.cloudTemporalEnabled = true;
    parameters.cloudQuality = tier;
    parameters.cloudFeatureScale = 2400.0f;
    parameters.cloudHorizonFadeDegrees = 0;
    Camera camera;
    camera.setOrbitPose(glm::vec3(0), -35, -35, 6, 55);
    cloud::MarchSettings settings;
    const auto budget = atmosphere::cloudTierBudget(tier);
    settings.primarySteps = budget.primarySteps;
    settings.lightSteps = budget.lightSteps;
    settings.spatialJitter = true;
    settings.sunDirection = atmosphere::sunDirection(parameters);
    CloudLayerRenderer layer(root / "shaders/fullscreen.vert", root / "shaders/cloud_layer.frag");
    auto draw = [&] {
        layer.render(camera, parameters, settings, glm::vec3(0.7f), glm::vec3(0.3f), width, height);
    };
    draw();
    require(layer.bufferWidth() == width / 2 && layer.bufferHeight() == height / 2,
        "half-resolution march must quarter the pixel count");
    require(!layer.lastHistoryReused(), "first frame must reject uninitialized history");
    auto lastRaw = read(layer.rawRadianceTexture(), width / 2, height / 2);
    auto lastResolved = read(layer.radianceTexture(), width / 2, height / 2);
    auto depths = read(layer.depthTexture(), width / 2, height / 2);
    std::size_t hits = 0;
    for (std::size_t i = 0; i < depths.size(); i += 4) {
        if (depths[i] > 0) {
            ++hits;
            require(depths[i] >= depths[i + 1], "first density depth must follow slab entry");
        }
    }
    require(hits > 100, "volume depth acceptance must exercise cloudy pixels");
    require(lastRaw == lastResolved, "first resolve must copy current samples exactly");
    double rawFlicker = 0, resolvedFlicker = 0;
    for (int frame = 1; frame <= 20; ++frame) {
        draw();
        require(layer.lastHistoryReused(), "stable camera must keep cloud history");
        auto raw = read(layer.rawRadianceTexture(), width / 2, height / 2);
        auto resolved = read(layer.radianceTexture(), width / 2, height / 2);
        if (frame > 4) {
            rawFlicker += alphaError(raw, lastRaw);
            resolvedFlicker += alphaError(resolved, lastResolved);
        }
        lastRaw = std::move(raw);
        lastResolved = std::move(resolved);
    }
    require(rawFlicker > 0.001, "temporal sample sequence must actually vary the march");
    require(resolvedFlicker < rawFlicker * 0.65,
        "independent history must reduce stable-camera transmission flicker by at least 35 percent");
    const std::string name = tier == atmosphere::CloudQualityTier::Low ? "low" : "high";
    save(output / (name + "-static-raw.ppm"), lastRaw, width / 2, height / 2);
    save(output / (name + "-static-resolved.ppm"), lastResolved, width / 2, height / 2);
    double movingError = 0;
    // A deliberately wrong baseline: accumulate at the same screen pixel with no reprojection,
    // depth rejection or clamp. Keep its failure visible, rather than only saving successful frames.
    auto naive = lastResolved;
    for (int frame = 0; frame < 12; ++frame) {
        camera.moveLocal(0, 40);
        parameters.cloudWindOffsetX += 8;
        draw();
        require(layer.lastHistoryReused(), "normal camera/wind motion should reproject, not globally reset");
        lastRaw = read(layer.rawRadianceTexture(), width / 2, height / 2);
        lastResolved = read(layer.radianceTexture(), width / 2, height / 2);
        for (std::size_t i = 0; i < naive.size(); ++i) naive[i] = naive[i] * 0.85f + lastRaw[i] * 0.15f;
        movingError = std::max(movingError, alphaError(lastRaw, lastResolved));
    }
    require(movingError < 0.06, "moving-camera history must stay close to current transmission");
    save(output / (name + "-moving-raw.ppm"), lastRaw, width / 2, height / 2);
    save(output / (name + "-moving-resolved.ppm"), lastResolved, width / 2, height / 2);
    save(output / (name + "-moving-naive.ppm"), naive, width / 2, height / 2);
    auto errorMap = lastRaw;
    for (std::size_t i = 0; i < errorMap.size(); i += 4) {
        const float error = std::abs(naive[i + 3] - lastRaw[i + 3]) * 8;
        errorMap[i] = errorMap[i + 1] = errorMap[i + 2] = error;
    }
    save(output / (name + "-moving-naive-error.ppm"), errorMap, width / 2, height / 2);
    std::cout << name << ": naive moving final transmission error " << alphaError(naive, lastRaw) << '\n';
    auto fresh = [&] {
        require(!layer.lastHistoryReused(), "discontinuous changes must reject cloud history");
        require(read(layer.rawRadianceTexture(), layer.bufferWidth(), layer.bufferHeight()) ==
            read(layer.radianceTexture(), layer.bufferWidth(), layer.bufferHeight()),
            "reset frames must contain no residual cloud history");
    };
    parameters.cloudOfflineNoise=true;
    draw(); fresh();
    draw();
    require(layer.lastHistoryReused(),"unchanged offline asset should reuse cloud history");
    parameters.cloudOfflineNoise=false;
    draw(); fresh();
    parameters.cloudCoverage = 0;
    parameters.cloudCoverageVariation = 0;
    draw(); fresh();
    auto clear = read(layer.radianceTexture(), width / 2, height / 2);
    for (std::size_t i = 3; i < clear.size(); i += 4) require(clear[i] == 1.0f,
        "removing cloud density must immediately remove history ghosts");
    parameters.cloudCoverage = 0.6f;
    draw(); fresh();
    draw();
    parameters.cloudWindOffsetX += parameters.cloudFeatureScale;
    draw(); fresh();
    draw();
    auto pose = camera.orbitState();
    pose.yawDegrees += 60;
    camera.setOrbitState(pose);
    draw(); fresh();
    draw();
    layer.render(camera, parameters, settings, glm::vec3(0.7f), glm::vec3(0.3f), width + 1, height + 1);
    fresh();
    require(layer.bufferWidth() == 97 && layer.bufferHeight() == 65, "odd sizes must round up");
    draw(); fresh();
    draw();
    layer.invalidateHistory();
    draw(); fresh();
    parameters.cloudsEnabled = false;
    draw();
    require(!layer.lastFrameActive(), "clouds off must skip rendering");
    parameters.cloudsEnabled = true;
    draw(); fresh();
    parameters.cloudTemporalEnabled = false;
    draw();
    auto deterministic = read(layer.radianceTexture(), width / 2, height / 2);
    draw();
    require(deterministic == read(layer.radianceTexture(), width / 2, height / 2),
        "history off must keep the fixed jitter sequence deterministic");
    require(glGetError() == GL_NO_ERROR, "cloud history draws must not leak GL errors");
    std::cout << name << ": raw/resolve static flicker " << rawFlicker / 16 << '/' << resolvedFlicker / 16
        << ", moving transmission error " << movingError << '\n';
}
}
int main(int argc, char** argv) {
    if (argc != 2 || glfwInit() != GLFW_TRUE) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(32, 32, "Cloud temporal acceptance", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(glfwGetProcAddress)) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    int result = 0;
    try {
        std::filesystem::create_directories(argv[1]);
        for (auto tier : {atmosphere::CloudQualityTier::Low, atmosphere::CloudQualityTier::High})
            exercise(MYRENDERER_SOURCE_DIR, argv[1], tier);
        std::cout << "Cloud temporal acceptance: PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
