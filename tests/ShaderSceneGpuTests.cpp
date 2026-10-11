#include "plugin/ShaderSceneRenderer.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "render/Camera.h"
#include "render/RenderTarget.h"
#include "scene/SceneDocument.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path, std::ios::trunc) << text;
    static int revision = 0;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++revision));
}
std::vector<unsigned char> pixels(const RenderTarget& target) {
    std::vector<unsigned char> image(static_cast<std::size_t>(target.width() * target.height() * 4));
    glBindTexture(GL_TEXTURE_2D, target.colorTexture());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data()); return image;
}
}
int main(int argc, char** argv) {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(64, 64, "ShaderScene test", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(glfwGetProcAddress)) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    int result = 0;
    const auto directory = std::filesystem::temp_directory_path() / ("IrisShaderSceneGpu-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try {
        check(argc == 2, "Shader directory required");
        std::filesystem::create_directories(directory / "package");
        const auto fragment = directory / "package/image.frag";
        const auto manifest = directory / "test.shaderscene";
        write(manifest, R"({"version":1,"id":"test","resourceRoot":"package","fragment":"image.frag","entry":"main","output":"display-rgba8","camera":"project","author":"test","license":"MIT","parameters":[{"id":"gain","label":"Gain","type":"float","default":0.5,"min":0,"max":1}]})");
        const std::string source = "#version 330 core\nout vec4 color;\nuniform float gain;uniform float irisTime;uniform vec2 irisResolution;uniform vec3 irisCameraPosition;\nvoid main(){color=vec4(gain, gl_FragCoord.x/irisResolution.x, irisTime + irisCameraPosition.x*0.01,1);}\n";
        write(fragment, source);
        SceneDocument document; std::string error;
        check(loadSceneDocument(manifest, document, error), error.c_str());
        RendererSettings settings = document.renderer;
        Camera camera; RenderTarget target; target.resize(64, 32, 1);
        unsigned int vao = 0; glGenVertexArrays(1, &vao);
        {
            iris::ShaderSceneRenderer runtime(std::filesystem::u8path(argv[1]));
            runtime.prepareResources(settings, 64, 32);
            auto draw = [&](float time) {
                runtime.renderFrame({target, camera, settings, target.width(), target.height(), time, vao});
                check(glGetError() == GL_NO_ERROR, "ShaderScene draw GL error"); return pixels(target);
            };
            const auto first = draw(0.25f); check(first == draw(0.25f), "Fixed frame differs");
            check(first[0] >= 127 && first[0] <= 128, "Parameter not consumed by GPU");
            check(first != draw(0.5f), "Time input has no contribution");
            camera.moveLocal(0, 1); check(first != draw(0.25f), "Camera input has no contribution"); camera.reset();
            check(saveSceneDocument(directory / "saved.myscene", document, error), error.c_str());
            SceneDocument reopened; check(loadSceneDocument(directory / "saved.myscene", reopened, error), error.c_str());
            check(reopened.renderer.shaderSceneResource == settings.shaderSceneResource, "Relative reference round trip failed");
            write(fragment, "#version 330 core\ninvalid shader\n");
            auto report = Shader::reloadChangedShaders();
            check(report.failed > 0 && report.retained > 0 && first == draw(0.25f), "Compile rejection lost old image");
            write(fragment, "#version 330 core\nout vec4 color;uniform vec3 gain;void main(){color=vec4(gain,1);}\n");
            report = Shader::reloadChangedShaders(true);
            check(report.failed > 0 && first == draw(0.25f), "Uniform mismatch lost old image");
            write(directory / "outside.glsl", "// Outside the declared package root\n");
            write(fragment, "#version 330 core\n#include \"../outside.glsl\"\nout vec4 color;void main(){color=vec4(1);}\n");
            report = Shader::reloadChangedShaders(true);
            check(report.failed > 0 && first == draw(0.25f), "Outside include accepted");
            write(fragment, source); report = Shader::reloadChangedShaders(true);
            check(report.failed == 0 && report.reloaded > 0 && first == draw(0.25f), "Shader recovery failed");
            target.resize(31, 47, 1); const auto resized = draw(0.25f);
            check(resized.size() == 31 * 47 * 4 && resized == draw(0.25f), "Resize output differs");
            write(fragment, "#version 330 core\ninvalid again\n");
            bool rejected = false;
            try { runtime.prepareResources(settings, 31, 47); } catch (const std::exception&) { rejected = true; }
            check(rejected && resized == draw(0.25f), "Failed package rebuild lost live program");
            write(fragment, source);
            write(fragment, "#version 330 core\nout vec4 color;uniform vec2 irisResolution;void main(){if(gl_FragCoord.x < irisResolution.x*0.5)discard;color=vec4(1,0,1,1);}\n");
            runtime.prepareResources(settings, 31, 47);
            const auto partial = draw(0.25f);
            check(partial[0] == 0 && partial[1] == 0 && partial[2] == 0 && partial[3] == 255
                && partial[partial.size() - 4] == 255 && partial == draw(0.25f), "Discard retained undeclared previous frame pixels");
        }
        glDeleteVertexArrays(1, &vao);
        check(glGetError() == GL_NO_ERROR, "Exit GL error");
        std::cout << "ShaderScene real GL: parameters/time/camera, fixed repeat, Scene reference, compile/type/include failure retention, recovery, resize, discard clear and destruction PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    std::filesystem::remove_all(directory);
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
