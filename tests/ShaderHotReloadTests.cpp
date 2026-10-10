#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "render/Shader.h"

namespace {

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Could not write " + path.string());
    output << text;
}

void advanceTimestamp(const std::filesystem::path& path, int seconds) {
    std::filesystem::last_write_time(
        path,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(seconds)
    );
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
    GLFWwindow* window = glfwCreateWindow(64, 64, "Shader reload test", nullptr, nullptr);
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

    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory = std::filesystem::temp_directory_path()
        / ("myrenderer-shader-reload-" + std::to_string(serial));
    std::filesystem::create_directories(directory);
    const std::filesystem::path vertexPath = directory / "reload.vert";
    const std::filesystem::path fragmentPath = directory / "reload.frag";

    int result = 0;
    try {
        writeText(vertexPath, R"(#version 330 core
layout(location = 0) in vec3 aPosition;
void main() { gl_Position = vec4(aPosition, 1.0); }
)");
        writeText(fragmentPath, R"(#version 330 core
out vec4 color;
void main() { color = vec4(1.0); }
)");

        {
            Shader shader(vertexPath, fragmentPath);
            shader.use();
            GLint originalProgram = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &originalProgram);

            writeText(fragmentPath, "#version 330 core\nthis is intentionally invalid\n");
            advanceTimestamp(fragmentPath, 2);
            const Shader::ReloadReport failed = Shader::reloadChangedShaders();
            shader.use();
            GLint retainedProgram = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &retainedProgram);
            if (failed.failed != 1U || failed.reloaded != 0U
                || failed.message.empty() || retainedProgram != originalProgram) {
                throw std::runtime_error("Failed shader reload did not retain the previous program");
            }

            writeText(fragmentPath, R"(#version 330 core
out vec4 color;
void main() { color = vec4(0.25, 0.5, 0.75, 1.0); }
)");
            advanceTimestamp(fragmentPath, 4);
            const Shader::ReloadReport recovered = Shader::reloadChangedShaders();
            shader.use();
            GLint replacementProgram = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &replacementProgram);
            if (recovered.reloaded != 1U || recovered.failed != 0U
                || replacementProgram == 0 || replacementProgram == originalProgram) {
                throw std::runtime_error("Corrected shader did not replace the previous program");
            }
        }
        // One valid pass and one broken pass must not publish a mixed pipeline.
        const auto otherPath = directory / "other.frag";
        const auto includePath = directory / "new-include.glsl";
        const std::string white = "#version 330 core\nout vec4 color;\nvoid main(){ color=vec4(1.0); }\n";
        const std::string colored = "#version 330 core\nout vec4 color;\nvoid main(){ color=vec4(0.2,0.5,0.7,1.0); }\n";
        writeText(fragmentPath, white); writeText(otherPath, white);
        {
            Shader first(vertexPath, fragmentPath);
            auto second = std::make_unique<Shader>(vertexPath, otherPath);
            const auto program = [](const Shader& shader) { shader.use(); GLint id=0; glGetIntegerv(GL_CURRENT_PROGRAM,&id); return id; };
            const auto firstProgram = program(first), secondProgram = program(*second);
            writeText(fragmentPath, colored); advanceTimestamp(fragmentPath, 10);
            writeText(includePath, "this include is invalid\n");
            writeText(otherPath, "#version 330 core\n#include \"new-include.glsl\"\nout vec4 color;\nvoid main(){color=replacementColor();}\n");
            advanceTimestamp(otherPath, 10);
            auto report = Shader::reloadChangedShaders();
            if (report.failed != 1 || report.reloaded != 0 || report.retained != 2
                || program(first) != firstProgram || program(*second) != secondProgram)
                throw std::runtime_error("Batch compile failure partially replaced programs");
            report = Shader::reloadChangedShaders();
            if (report.failed || report.reloaded) throw std::runtime_error("Unchanged failure retried every poll");
            report = Shader::reloadChangedShaders(true);
            if (report.failed != 1 || report.retained != 2 || report.reloaded)
                throw std::runtime_error("Explicit retry did not retain complete batch");
            // Repair only a newly introduced include, without touching its parent.
            writeText(includePath, "vec4 replacementColor(){return vec4(0.1,0.8,0.2,1.0);}\n");
            advanceTimestamp(includePath, 12);
            report = Shader::reloadChangedShaders();
            if (report.failed || report.reloaded != 2 || program(first) == firstProgram || program(*second) == secondProgram)
                throw std::runtime_error("Included-file repair did not recover both pending programs");
            const auto recoveredFirst = program(first), recoveredSecond = program(*second);
            // Missing resources must also be watched, so creating one is a recovery event.
            writeText(fragmentPath, white); advanceTimestamp(fragmentPath, 14);
            writeText(otherPath, "#version 330 core\n#include \"missing-created.glsl\"\nout vec4 color;\nvoid main(){color=replacementColor();}\n");
            advanceTimestamp(otherPath, 14);
            report = Shader::reloadChangedShaders();
            if (report.failed != 1 || report.reloaded || report.retained != 2
                || program(first) != recoveredFirst || program(*second) != recoveredSecond)
                throw std::runtime_error("Missing include published a partial batch");
            writeText(directory / "missing-created.glsl", "vec4 replacementColor(){return vec4(0.8);}\n");
            report = Shader::reloadChangedShaders();
            if (report.failed || report.reloaded != 2) throw std::runtime_error("Creating missing include did not recover");
            // Both stages compile, but conflicting active uniform types fail at link.
            const auto retainedFirst = program(first), retainedSecond = program(*second);
            writeText(vertexPath, "#version 330 core\nlayout(location=0) in vec3 aPosition;\nuniform float uConflict;\nvoid main(){gl_Position=vec4(aPosition*uConflict,1.0);}\n");
            advanceTimestamp(vertexPath, 16);
            writeText(fragmentPath, colored); advanceTimestamp(fragmentPath, 16);
            writeText(otherPath, "#version 330 core\nuniform vec2 uConflict;\nout vec4 color;\nvoid main(){color=vec4(uConflict,0.0,1.0);}\n");
            advanceTimestamp(otherPath, 16);
            report = Shader::reloadChangedShaders();
            if (report.failed != 1 || report.reloaded || program(first) != retainedFirst || program(*second) != retainedSecond)
                throw std::runtime_error("Link failure did not retain whole batch: failed="+std::to_string(report.failed)
                    +", reloaded="+std::to_string(report.reloaded)+", "+report.message);
            // Removing a blocking owner permits the surviving pending shader to commit.
            second.reset();
            report = Shader::reloadChangedShaders();
            if (report.failed || report.reloaded != 1 || program(first) == retainedFirst)
                throw std::runtime_error("Destroyed blocking shader left a pending transaction stuck");
            if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Shader transaction GL error");
        }
        // Constructor failure in a later geometry/fragment stage must clean earlier stages.
        writeText(directory / "bad.geom", "#version 330 core\ninvalid geometry\n");
        for (int i=0; i<8; ++i) {
            bool rejected=false;
            try { Shader failed(vertexPath, directory / "bad.geom", fragmentPath); }
            catch (const std::runtime_error&) { rejected=true; }
            if (!rejected || glGetError()!=GL_NO_ERROR) throw std::runtime_error("Geometry failure recovery invalid");
        }
        writeText(directory / "bad.geom", "#version 330 core\nlayout(triangles) in;\nlayout(triangle_strip,max_vertices=3) out;\nvoid main(){for(int i=0;i<3;++i){gl_Position=gl_in[i].gl_Position;EmitVertex();}EndPrimitive();}\n");
        {
            Shader geometry(vertexPath,directory/"bad.geom",fragmentPath);
            geometry.use(); GLint original=0; glGetIntegerv(GL_CURRENT_PROGRAM,&original);
            writeText(fragmentPath,"#version 330 core\ninvalid fragment\n"); advanceTimestamp(fragmentPath,20);
            const auto report=Shader::reloadChangedShaders(); geometry.use(); GLint retained=0; glGetIntegerv(GL_CURRENT_PROGRAM,&retained);
            if(report.failed!=1 || report.reloaded || retained!=original) throw std::runtime_error("Geometry program transaction failed");
        }
        const auto canceled=Shader::reloadChangedShaders();
        if(canceled.pending || canceled.failed || canceled.reloaded) throw std::runtime_error("Destroyed failed shaders remained pending");
        std::cout << "Shader hot reload fail-safe and recovery passed\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        result = 1;
    }

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
