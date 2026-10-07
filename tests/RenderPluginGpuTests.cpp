#include "plugin/RenderPluginRegistry.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "render/RenderTarget.h"

#include <iostream>
#include <stdexcept>
#include <vector>
#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<unsigned char> capture(iris::RenderPlugin& plugin,
    RenderTarget& target, const Camera& camera, const RendererSettings& settings,
    unsigned int vao, int width, int height) {
    target.resize(width, height, 1);
    for (int index = 0; index < 64; ++index)
        plugin.renderFrame({target, camera, settings, width, height, 1.25f, vao});
    target.bindFinal();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    require(glGetError() == GL_NO_ERROR, "GL error during plugin capture");
    return pixels;
}
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(64, 64, "Plugin lifecycle validation", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(glfwGetProcAddress)) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    int result = 0;
    try {
        unsigned int vao = 0;
        glGenVertexArrays(1, &vao);
        {
            Camera camera;
            camera.setOrbitPose(glm::vec3(0, 1.6f, -30), -4, -3, 16, 45);
            RendererSettings settings;
            settings.enscapeCube.noiseReduction = true;
            settings.enscapeCube.cubeEnabled = false;
            RenderTarget target;
            auto plugin = iris::builtinRenderPlugins().create(iris::enscapePluginId,
                {iris::openGlFullscreenService}, std::filesystem::path(MYRENDERER_SOURCE_DIR) / "shaders");
            const auto first = capture(*plugin, target, camera, settings, vao, 480, 270);
            plugin->invalidateHistory();
            require(capture(*plugin, target, camera, settings, vao, 480, 270) == first,
                "History reset did not reproduce fixed output");
            capture(*plugin, target, camera, settings, vao, 160, 90);
            require(capture(*plugin, target, camera, settings, vao, 480, 270) == first,
                "Resize roundtrip changed fixed output");
            auto moved = camera;
            moved.orbit(0.15f, 0.05f);
            require(capture(*plugin, target, moved, settings, vao, 480, 270) != first,
                "Plugin ignored camera input");
            settings.enscapeCube.noiseReduction = false;
            require(capture(*plugin, target, camera, settings, vao, 480, 270) != first,
                "Plugin ignored parameter input");
            settings.enscapeCube.noiseReduction = true;
            require(capture(*plugin, target, camera, settings, vao, 480, 270) == first,
                "Camera/parameter restoration retained stale history");
            const auto info = plugin->frameInfo();
            require(info.passNames.size() == 4 && info.drawCalls == 4,
                "Plugin pass report incorrect");
            require(info.gpuTimeValid && info.gpuMilliseconds > 0,
                "Plugin GPU measurements missing");
            plugin.reset();
            require(glGetError() == GL_NO_ERROR, "Plugin destruction produced GL error");
            plugin = iris::builtinRenderPlugins().create(iris::enscapePluginId,
                {iris::openGlFullscreenService}, std::filesystem::path(MYRENDERER_SOURCE_DIR) / "shaders");
            require(capture(*plugin, target, camera, settings, vao, 480, 270) == first,
                "Plugin recreation changed output");
            std::string error;
            require(target.savePng(std::filesystem::path(MYRENDERER_PLUGIN_OUTPUT) / "lifecycle.png", error),
                "Could not save lifecycle evidence");
        }
        glDeleteVertexArrays(1, &vao);
        require(glGetError() == GL_NO_ERROR, "Plugin teardown GL error");
        std::cout << "Plugin GPU lifecycle/reset/resize/camera/parameters/recreation: PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
