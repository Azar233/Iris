#include "plugin/RenderPluginRegistry.h"
#include "render/Renderer.h"
#include "WaterRenderer.h"
#include "render/Shader.h"
#include "render/Camera.h"
#include "render/RenderTarget.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <array>
#include <fstream>
#include <chrono>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F function, const char* message = "Invalid water transaction accepted") {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
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
std::array<GLuint,2> programs(const iris::RenderPlugin& plugin) {
    GLint previous = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
    std::array<GLuint,2> result{};
    for (int path = 0; path < 2; ++path) {
        auto* shader = plugin.borrowShadingShader(path != 0);
        require(shader != nullptr, "Missing owned water shader"); shader->use();
        GLint program = 0, linked = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        require(linked == GL_TRUE, "Unlinked owned water program");
        result[path] = static_cast<GLuint>(program);
    }
    glUseProgram(previous); return result;
}
void alive(const std::array<GLuint,2>& values, bool expected) {
    require(bool(glIsProgram(values[0])) == expected && bool(glIsProgram(values[1])) == expected,
        "Water program lifetime mismatch");
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
        const auto source = std::filesystem::path(MYRENDERER_SOURCE_DIR) / "shaders";
        const auto directory = std::filesystem::path(IRIS_WATER_TEST_DIRECTORY);
        std::filesystem::create_directories(directory);
        for (const auto* file : {"water.vert", "water_forward.frag", "water_gbuffer.frag", "cloud_shadow_sample.glsl", "ocean_snoise.glsl"})
            std::filesystem::copy_file(source / file, directory / file, std::filesystem::copy_options::overwrite_existing);
        Shader sentinelShader(source / "water.vert", source / "water_forward.frag"); sentinelShader.use();
        GLint sentinelProgram = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &sentinelProgram);
        GLuint sentinelVao = 0, sentinelBuffer = 0;
        glGenVertexArrays(1, &sentinelVao); glGenBuffers(1, &sentinelBuffer);
        glBindVertexArray(sentinelVao); glBindBuffer(GL_ARRAY_BUFFER, sentinelBuffer);
        auto bindings = [&] {
            GLint vao = 0, array = 0, program = 0;
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao); glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array);
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            require(static_cast<GLuint>(vao) == sentinelVao && static_cast<GLuint>(array) == sentinelBuffer,
                "Preparation changed borrowed bindings");
            require(program == sentinelProgram, "Preparation changed borrowed program");
            require(glGetError() == GL_NO_ERROR, "Water transaction GL error");
        };
        iris::RenderPluginRegistry registry;
        iris::WaterRenderer* candidate = nullptr;
        int factoryCalls = 0;
        registry.add({iris::waterPluginId, iris::renderPluginApiVersion,
            {iris::openGlShadingService, iris::openGlIndexedGeometryService, iris::openGlShaderBorrowService}, "MIT",
            {iris::PluginStage::MaterialShading, {}, {}}, iris::builtinRenderPluginParameterSchema(iris::waterPluginId)},
            [&](const std::filesystem::path& shaders) {
                ++factoryCalls;
                auto plugin = std::make_unique<iris::WaterRenderer>(shaders); candidate = plugin.get(); return plugin;
            });
        RendererSettings settings; settings.water.quality = WaterQuality::High;
        const std::vector<std::string> services{iris::openGlShadingService, iris::openGlIndexedGeometryService, iris::openGlShaderBorrowService};
        auto prepare = [&](const RendererSettings& value, const std::function<bool()>& current = {}) {
            return registry.prepareReplacement(iris::waterPluginId, services, directory, value, 64, 64, current);
        };
        rejects([&] { registry.prepareReplacement(iris::waterPluginId, {iris::openGlShadingService}, {}, settings, 64, 64); });
        rejects([&] { registry.prepareReplacement(iris::waterPluginId,
            {iris::openGlShadingService, iris::openGlIndexedGeometryService}, directory, settings, 64, 64); });
        require(factoryCalls == 0, "Missing service allocated water resources"); bindings();
        auto owner = prepare(settings); bindings();
        auto ownedPrograms = programs(*owner); bindings();
        Camera camera; RenderTarget target;
        const iris::RenderPluginFrame foreignShaderFrame{target, camera, settings, 64, 64, 0, 0, nullptr, &sentinelShader};
        rejects([&] { owner->renderFrame(foreignShaderFrame); }, "Foreign shader accepted by water uniforms");
        bindings();
        auto high = inspect(owner->borrowIndexedGeometry(settings), water::gridResolution);
        settings.water.quality = WaterQuality::Low;
        auto low = inspect(owner->borrowIndexedGeometry(settings), water::lowGridResolution);
        require(owner->estimatedBytes() == bytes(water::gridResolution) + bytes(water::lowGridResolution), "Wrong owned memory");
        owner->prepareResources(settings, 128, 128); bindings();
        require(owner->borrowIndexedGeometry(settings).vertexArray == low.vao, "Resize recreated fixed grid");
        require(programs(*owner) == ownedPrograms, "Resize recreated water programs");
        auto invalid = settings; invalid.water.amplitude = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { prepare(invalid); }); bindings(); alive(high, true); alive(low, true);
        const auto fragment = directory / "water_gbuffer.frag";
        const auto fragmentTime = std::filesystem::last_write_time(fragment);
        const auto vertexTime = std::filesystem::last_write_time(directory / "water.vert");
        std::ofstream(fragment) << "#version 330 core\n#error intentional second-program compile failure\n";
        // MinGW file times can be coarser than MSVC's. Make source changes
        // observable without a wall-clock sleep or compiler-specific timing.
        std::filesystem::last_write_time(fragment, fragmentTime + std::chrono::seconds(2));
        rejects([&] { prepare(settings); }, "Second water program compile failure accepted"); bindings(); alive(ownedPrograms, true);
        std::ofstream(directory / "water.vert", std::ios::app) << "\n// force both water programs into the reload transaction\n";
        std::filesystem::last_write_time(directory / "water.vert", vertexTime + std::chrono::seconds(2));
        const auto failedReload = Shader::reloadChangedShaders();
        require(failedReload.failed != 0, "Shader source change was not detected");
        require(programs(*owner) == ownedPrograms, "Failed reload replaced old water programs");
        bindings(); alive(ownedPrograms, true);
        std::filesystem::copy_file(source / "water_gbuffer.frag", fragment, std::filesystem::copy_options::overwrite_existing);
        const auto recovered = Shader::reloadChangedShaders(true);
        require(recovered.failed == 0 && recovered.reloaded == 2, "Owned water programs did not recover in atomic reload");
        const auto recoveredPrograms = programs(*owner);
        alive(ownedPrograms, false); ownedPrograms = recoveredPrograms; bindings();
        rejects([&] { registry.prepareReplacement(iris::waterPluginId, services, directory / "missing", settings, 64, 64); });
        bindings(); alive(ownedPrograms, true); alive(high, true); alive(low, true);
        Grid canceledHigh{}, canceledLow{}; int checks = 0;
        std::array<GLuint,2> canceledPrograms{};
        rejects([&] { prepare(settings, [&] {
            if (++checks == 1) return true;
            auto quality = settings; quality.water.quality = WaterQuality::High;
            canceledHigh = inspect(candidate->borrowIndexedGeometry(quality), water::gridResolution);
            canceledLow = inspect(candidate->borrowIndexedGeometry(settings), water::lowGridResolution);
            canceledPrograms = programs(*candidate);
            return false;
        }); });
        require(checks == 2, "Cancellation did not reach prepared candidate");
        bindings(); alive(canceledHigh, false); alive(canceledLow, false); alive(high, true); alive(low, true);
        alive(canceledPrograms, false); alive(ownedPrograms, true);
        auto replacement = prepare(settings); bindings();
        const auto replacementPrograms = programs(*replacement);
        auto replacementLow = inspect(replacement->borrowIndexedGeometry(settings), water::lowGridResolution);
        auto quality = settings; quality.water.quality = WaterQuality::High;
        auto replacementHigh = inspect(replacement->borrowIndexedGeometry(quality), water::gridResolution);
        owner = std::move(replacement);
        alive(ownedPrograms, false); alive(replacementPrograms, true);
        alive(high, false); alive(low, false); alive(replacementHigh, true); alive(replacementLow, true);
        owner.reset(); alive(replacementHigh, false); alive(replacementLow, false); bindings();
        alive(replacementPrograms, false);
        auto restarted = prepare(settings); bindings();
        const auto restartedPrograms = programs(*restarted);
        auto restartedLow = inspect(restarted->borrowIndexedGeometry(settings), water::lowGridResolution);
        restarted.reset(); alive(restartedLow, false);
        alive(restartedPrograms, false);
        glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDeleteBuffers(1, &sentinelBuffer); glDeleteVertexArrays(1, &sentinelVao);
        std::cout << "Water grid/program ownership, compile failure, atomic reload, replacement, cancellation and bindings: PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
