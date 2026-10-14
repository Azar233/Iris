#include "plugin/RenderPluginRegistry.h"
#include "render/Renderer.h"
#include "WaterRenderer.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F function) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid water transaction accepted");
}
struct Grid { GLuint vao, vertices, indices; };
Grid inspect(const iris::IndexedTriangleGeometry& geometry, int resolution) {
    iris::validateIndexedTriangleGeometry(geometry);
    require(glIsVertexArray(geometry.vertexArray), "Missing grid VAO");
    require(geometry.indexCount == static_cast<std::size_t>(resolution * resolution * 6), "Wrong index count");
    GLint previous = 0, vertices = 0, indices = 0, size = 0, enabled = 0, components = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous);
    glBindVertexArray(geometry.vertexArray);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &vertices);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &components);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &indices);
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
    require(enabled && components == 2 && vertices && indices, "Invalid grid attributes");
    require(size == resolution * resolution * 6 * 4, "Wrong index storage");
    GLint array = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array);
    glBindBuffer(GL_ARRAY_BUFFER, vertices);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
    require(size == (resolution + 1) * (resolution + 1) * 2 * 4, "Wrong vertex storage");
    glBindBuffer(GL_ARRAY_BUFFER, array);
    glBindVertexArray(previous);
    return {geometry.vertexArray, static_cast<GLuint>(vertices), static_cast<GLuint>(indices)};
}
void alive(const Grid& grid, bool expected) {
    require(bool(glIsVertexArray(grid.vao)) == expected && bool(glIsBuffer(grid.vertices)) == expected
        && bool(glIsBuffer(grid.indices)) == expected, "Grid lifetime mismatch");
}
std::size_t bytes(int resolution) {
    return static_cast<std::size_t>((resolution + 1) * (resolution + 1) * 8 + resolution * resolution * 24);
}
}
int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(64, 64, "Water grid ownership", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(glfwGetProcAddress)) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    int result = 0;
    try {
        GLuint sentinelVao = 0, sentinelBuffer = 0;
        glGenVertexArrays(1, &sentinelVao); glGenBuffers(1, &sentinelBuffer);
        glBindVertexArray(sentinelVao); glBindBuffer(GL_ARRAY_BUFFER, sentinelBuffer);
        auto bindings = [&] {
            GLint vao = 0, array = 0;
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao); glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array);
            require(static_cast<GLuint>(vao) == sentinelVao && static_cast<GLuint>(array) == sentinelBuffer,
                "Preparation changed borrowed bindings");
            require(glGetError() == GL_NO_ERROR, "Water transaction GL error");
        };
        iris::RenderPluginRegistry registry;
        iris::WaterRenderer* candidate = nullptr;
        int factoryCalls = 0;
        registry.add({iris::waterPluginId, iris::renderPluginApiVersion,
            {iris::openGlShadingService, iris::openGlIndexedGeometryService}, "MIT",
            {iris::PluginStage::MaterialShading, {}, {}}, iris::builtinRenderPluginParameterSchema(iris::waterPluginId)},
            [&](const std::filesystem::path&) {
                ++factoryCalls;
                auto plugin = std::make_unique<iris::WaterRenderer>(); candidate = plugin.get(); return plugin;
            });
        RendererSettings settings; settings.water.quality = WaterQuality::High;
        const std::vector<std::string> services{iris::openGlShadingService, iris::openGlIndexedGeometryService};
        auto prepare = [&](const RendererSettings& value, const std::function<bool()>& current = {}) {
            return registry.prepareReplacement(iris::waterPluginId, services, {}, value, 64, 64, current);
        };
        rejects([&] { registry.prepareReplacement(iris::waterPluginId, {iris::openGlShadingService}, {}, settings, 64, 64); });
        require(factoryCalls == 0, "Missing service allocated water resources"); bindings();
        auto owner = prepare(settings); bindings();
        auto high = inspect(owner->borrowIndexedGeometry(settings), water::gridResolution);
        settings.water.quality = WaterQuality::Low;
        auto low = inspect(owner->borrowIndexedGeometry(settings), water::lowGridResolution);
        require(owner->estimatedBytes() == bytes(water::gridResolution) + bytes(water::lowGridResolution), "Wrong owned memory");
        owner->prepareResources(settings, 128, 128); bindings();
        require(owner->borrowIndexedGeometry(settings).vertexArray == low.vao, "Resize recreated fixed grid");
        auto invalid = settings; invalid.water.amplitude = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { prepare(invalid); }); bindings(); alive(high, true); alive(low, true);
        Grid canceledHigh{}, canceledLow{}; int checks = 0;
        rejects([&] { prepare(settings, [&] {
            if (++checks == 1) return true;
            auto quality = settings; quality.water.quality = WaterQuality::High;
            canceledHigh = inspect(candidate->borrowIndexedGeometry(quality), water::gridResolution);
            canceledLow = inspect(candidate->borrowIndexedGeometry(settings), water::lowGridResolution);
            return false;
        }); });
        require(checks == 2, "Cancellation did not reach prepared candidate");
        bindings(); alive(canceledHigh, false); alive(canceledLow, false); alive(high, true); alive(low, true);
        auto replacement = prepare(settings); bindings();
        auto replacementLow = inspect(replacement->borrowIndexedGeometry(settings), water::lowGridResolution);
        auto quality = settings; quality.water.quality = WaterQuality::High;
        auto replacementHigh = inspect(replacement->borrowIndexedGeometry(quality), water::gridResolution);
        owner = std::move(replacement);
        alive(high, false); alive(low, false); alive(replacementHigh, true); alive(replacementLow, true);
        owner.reset(); alive(replacementHigh, false); alive(replacementLow, false); bindings();
        auto restarted = prepare(settings); bindings();
        auto restartedLow = inspect(restarted->borrowIndexedGeometry(settings), water::lowGridResolution);
        restarted.reset(); alive(restartedLow, false);
        glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDeleteBuffers(1, &sentinelBuffer); glDeleteVertexArrays(1, &sentinelVao);
        std::cout << "Water grid ownership, replacement, cancellation and bindings: PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
