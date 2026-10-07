#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "optics/CloudReference.h"
#include "render/CloudShadowRenderer.h"
#include "render/Shader.h"
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<float> read(const CloudShadowRenderer& map) {
    std::vector<float> values(static_cast<std::size_t>(map.resolution()) * map.resolution());
    glBindTexture(GL_TEXTURE_2D, map.texture());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, values.data());
    return values;
}
void save(const std::filesystem::path& path, const std::vector<float>& values, int size) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << size << ' ' << size << "\n255\n";
    for (int y = size - 1; y >= 0; --y) for (int x = 0; x < size; ++x) {
        const auto byte = static_cast<unsigned char>(std::clamp(values[static_cast<std::size_t>(y) * size + x], 0.0f, 1.0f) * 255 + 0.5f);
        const unsigned char pixel[] = {byte,byte,byte};
        file.write(reinterpret_cast<const char*>(pixel), 3);
    }
    require(static_cast<bool>(file), "could not write cloud shadow evidence");
}
void run(const std::filesystem::path& output) {
    atmosphere::AtmosphereParameters p;
    p.enabled = true;
    atmosphere::applyCloudPreset(p, atmosphere::CloudPreset::Cumulus);
    p.cloudShadowsEnabled = true;
    p.cloudFeatureScale = 1200;
    p.sunElevationDegrees = 45;
    p.sunAzimuthDegrees = 130;
    p.cloudOfflineNoise=std::getenv("MYRENDERER_TEST_OFFLINE_NOISE")!=nullptr;
    p.cloudNoisePeriod = p.cloudOfflineNoise?4.0f:5.0f;
    CloudShadowRenderer map(std::filesystem::path(MYRENDERER_SOURCE_DIR) / "shaders");
    for (auto tier : {atmosphere::CloudQualityTier::Low, atmosphere::CloudQualityTier::High}) {
        p.cloudQuality = tier;
        p.cloudWindOffsetX = 0;
        map.render(p, glm::vec3(0), 6000, cloud::volumetricExtinction);
        require(map.active(), "daylight cloud shadow map must run");
        require(map.updatedThisFrame(), "first map or quality change must render a new transmission map");
        const auto pixels = read(map);
        float worst = 0;
        int dark = 0;
        for (int y = 3; y < map.resolution(); y += 9) for (int x = 4; x < map.resolution(); x += 11) {
            auto origin = map.texelRayOrigin(x,y);
            const auto sun = atmosphere::sunDirection(p);
            origin -= sun * (origin.y / sun.y); // Equivalent receiver on y=0, below the cloud base.
            const float cpu = cloud::shadowTransmittance(origin, p, map.steps(), cloud::volumetricExtinction);
            const float gpu = pixels[static_cast<std::size_t>(y) * map.resolution() + x];
            require(std::isfinite(gpu) && gpu >= 0 && gpu <= 1, "GPU cloud transmission must be finite and bounded");
            worst = std::max(worst, std::abs(cpu - gpu));
            dark += gpu < 0.8f;
        }
        require(dark > 10, "CPU/GPU shadow parity must exercise covered sun rays");
        require(worst < 0.002f, "GPU cloud shadow must agree with the CPU sun integral");
        const std::string name = tier == atmosphere::CloudQualityTier::Low ? "low" : "high";
        save(output / (name + "-base.ppm"), pixels, map.resolution());
        map.render(p, glm::vec3(0.001f), 6000, cloud::volumetricExtinction);
        require(!map.updatedThisFrame(), "unchanged snapped inputs must reuse the existing map");
        require(pixels == read(map), "sub-texel camera movement must keep the snapped shadow grid stable");
        p.cloudWindOffsetX = 700;
        map.render(p, glm::vec3(0), 6000, cloud::volumetricExtinction);
        require(map.updatedThisFrame(), "wind changes must invalidate the map cache");
        const auto moved = read(map);
        double delta = 0;
        for (std::size_t i = 0; i < moved.size(); ++i) delta += std::abs(moved[i] - pixels[i]);
        delta /= static_cast<double>(moved.size());
        require(delta > 0.01, "wind must translate the actual cloud shadow field");
        save(output / (name + "-wind.ppm"), moved, map.resolution());
        p.sunAzimuthDegrees += 25;
        map.render(p, glm::vec3(0), 6000, cloud::volumetricExtinction);
        require(map.updatedThisFrame(), "sun changes must invalidate the map cache");
        require(moved != read(map), "solar direction must affect the cloud projection");
        p.sunAzimuthDegrees -= 25;
        std::cout << name << ": CPU/GPU maximum error " << worst << ", wind transmission MAE " << delta << '\n';
    }
    p.cloudCoverage = 0;
    p.cloudCoverageVariation = 0;
    map.render(p, glm::vec3(0), 6000, cloud::volumetricExtinction);
    for (float t : read(map)) require(t == 1, "removing clouds must immediately clear cloud shadows");
    p.sunElevationDegrees = -10;
    map.render(p, glm::vec3(0), 6000, cloud::volumetricExtinction);
    require(!map.active(), "night must skip solar cloud shadows");
    p.sunElevationDegrees = 45;
    p.cloudShadowsEnabled = false;
    map.render(p, glm::vec3(0), 6000, cloud::volumetricExtinction);
    require(!map.active(), "cloud shadow switch must disable the map");
    // A changed program must rerender even when every density/projection input
    // remains identical. Only a temporary shader copy is edited here.
    const auto shaderRoot = std::filesystem::path(MYRENDERER_SOURCE_DIR) / "shaders";
    const auto reloadRoot = output / "reload" / "shaders";
    std::filesystem::create_directories(reloadRoot);
    for (const char* file : {"fullscreen.vert", "cloud_shadow.frag", "cloud_noise_sample.glsl"}) {
        std::filesystem::copy_file(shaderRoot / file, reloadRoot / file,
            std::filesystem::copy_options::overwrite_existing);
    }
    {
        auto reloadParameters = p;
        atmosphere::applyCloudPreset(reloadParameters, atmosphere::CloudPreset::Cumulus);
        reloadParameters.enabled = reloadParameters.cloudShadowsEnabled = true;
        CloudShadowRenderer reloadMap(reloadRoot);
        reloadMap.render(reloadParameters, glm::vec3(0), 6000, cloud::volumetricExtinction);
        const auto original = read(reloadMap);
        reloadMap.render(reloadParameters, glm::vec3(0), 6000, cloud::volumetricExtinction);
        require(!reloadMap.updatedThisFrame(), "unchanged reload test input must hit the cache");
        const auto fragment = reloadRoot / "cloud_shadow.frag";
        { std::ofstream file(fragment, std::ios::app); file << "\n// Cache hot-reload acceptance\n"; }
        std::filesystem::last_write_time(fragment,
            std::filesystem::last_write_time(fragment) + std::chrono::seconds(2));
        const auto reload = Shader::reloadChangedShaders();
        require(reload.reloaded >= 1 && reload.failed == 0, "temporary cloud shader reload must succeed");
        reloadMap.render(reloadParameters, glm::vec3(0), 6000, cloud::volumetricExtinction);
        require(reloadMap.updatedThisFrame(), "hot reload must invalidate cached cloud transmission");
        require(original == read(reloadMap), "comment-only reload must preserve transmission pixels");
        reloadMap.render(reloadParameters, glm::vec3(0), 7000, cloud::volumetricExtinction);
        require(reloadMap.updatedThisFrame(), "projection extent changes must invalidate cached transmission");
        require(original != read(reloadMap), "projection extent must affect sampled cloud rays");
        reloadParameters.cloudShapeBlend = 0.85f;
        reloadMap.render(reloadParameters, glm::vec3(0), 6000, cloud::volumetricExtinction);
        require(reloadMap.updatedThisFrame() && original != read(reloadMap),
            "Perlin shape changes must invalidate and alter cloud transmission");
    }
    require(glGetError() == GL_NO_ERROR, "cloud shadow map must not leak GL errors");
}
}
int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(32,32,"Cloud shadow acceptance",nullptr,nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(glfwGetProcAddress)) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    int result = 0;
    try {
        std::filesystem::create_directories(argv[1]);
        run(argv[1]);
        std::cout << "Cloud shadow acceptance: PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
