#include "asset/InputManifest.h"
#include "render/Shader.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

namespace {

std::vector<Shader*>& shaderRegistry() {
    static std::vector<Shader*> registry;
    return registry;
}

std::filesystem::file_time_type shaderWriteTime(const std::filesystem::path& path) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    return error ? std::filesystem::file_time_type::min() : time;
}

} // namespace

std::string Shader::expandIncludes(
    const std::filesystem::path& path,
    int depth,
    std::vector<std::filesystem::path>& dependencies
) {
    // GLSL 3.30 has no `#include`; 4.6 added one and the project's contract is 3.30 Core. Expanding
    // it here is what lets a shader and the C++ code that has to agree with it read the *same file*
    // -- which is the only way a CPU reference and a GPU pass are guaranteed to evaluate one model
    // instead of two transcriptions of it.
    //
    // The directive form is deliberately narrow: a whole line that is exactly `#include "name"`.
    // Anything else on the line is left alone, and a missing file is a hard error rather than a
    // silently empty include, because a shader that lost half its model would still compile.
    constexpr int maximumDepth = 8;
    if (depth > maximumDepth) {
        throw std::runtime_error("Shader include nesting is too deep at " + path.string());
    }
    if (std::find(dependencies.begin(), dependencies.end(), path) == dependencies.end()) {
        dependencies.push_back(path);
    }
    std::istringstream input(readFile(path));
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t firstToken = line.find_first_not_of(" \t");
        const bool isInclude = firstToken != std::string::npos
            && line.compare(firstToken, 8, "#include") == 0;
        if (!isInclude) {
            output << line << '\n';
            continue;
        }
        const std::size_t quote = line.find('"', firstToken + 8);
        const std::size_t closing = quote == std::string::npos
            ? std::string::npos : line.find('"', quote + 1);
        if (quote == std::string::npos || closing == std::string::npos) {
            output << line << '\n';
            continue;
        }
        const std::string requested = line.substr(quote + 1, closing - quote - 1);
        // Two search rules, in order. Relative to the including file first, which is what a shader
        // next to the file it uses should write. Then relative to each ancestor directory, which is
        // what lets a shader reach a source file by the same `src/...` path the rest of the build
        // uses -- the project already resolves assets relative to the executable or the source root
        // rather than to the including file, so both habits exist here and a shader should not have
        // to know which one it is standing in.
        std::filesystem::path resolved = path.parent_path() / requested;
        if (!std::filesystem::is_regular_file(resolved)) {
            constexpr int maximumAncestors = 6;
            std::filesystem::path directory = path.parent_path();
            for (int level = 0; level < maximumAncestors; ++level) {
                const std::filesystem::path candidate = directory / requested;
                if (std::filesystem::is_regular_file(candidate)) {
                    resolved = candidate;
                    break;
                }
                if (!directory.has_parent_path() || directory.parent_path() == directory) break;
                directory = directory.parent_path();
            }
        }
        if (!std::filesystem::is_regular_file(resolved)) {
            throw std::runtime_error("Shader include not found: " + requested + " (from "
                + path.string() + ")");
        }
        // Each included file is wrapped in a `#line` pair so a compiler error inside it still names
        // the real file and line rather than a position in the expanded text.
        output << "#line 1 \"" << resolved.generic_string() << "\"\n";
        output << expandIncludes(resolved, depth + 1, dependencies);
        output << "#line 1 \"" << path.generic_string() << "\"\n";
    }
    return output.str();
}

Shader::Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath) {
    stagePaths_[0] = vertexPath;
    stagePaths_[2] = fragmentPath;
    // Expanded once here and reused at every point that needs the text, so the compile step, the
    // reload step and the dependency list can never disagree about what the shader is. One
    // dependency list per stage keeps a shared header's change attributed to the stages that
    // actually include it.
    const std::string vertexSource = expandIncludes(vertexPath, 0, includedPaths_[0]);
    const std::string fragmentSource = expandIncludes(fragmentPath, 0, includedPaths_[2]);
    const unsigned int vertexShader = compile(GL_VERTEX_SHADER, vertexSource, vertexPath);
    const unsigned int fragmentShader = compile(GL_FRAGMENT_SHADER, fragmentSource, fragmentPath);

    program_ = glCreateProgram();
    glAttachShader(program_, vertexShader);
    glAttachShader(program_, fragmentShader);
    glLinkProgram(program_);

    int linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        int logLength = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(static_cast<std::size_t>(std::max(logLength, 1)));
        glGetProgramInfoLog(program_, logLength, nullptr, log.data());
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(program_);
        program_ = 0;
        throw std::runtime_error("Shader link failed:\n" + std::string(log.data()));
    }

    glDetachShader(program_, vertexShader);
    glDetachShader(program_, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    captureWriteTimes();
    shaderRegistry().push_back(this);
}

Shader::Shader(
    const std::filesystem::path& vertexPath,
    const std::filesystem::path& geometryPath,
    const std::filesystem::path& fragmentPath
) {
    stagePaths_[0] = vertexPath;
    stagePaths_[1] = geometryPath;
    stagePaths_[2] = fragmentPath;
    hasGeometryStage_ = true;
    const unsigned int vertexShader = compile(
        GL_VERTEX_SHADER, expandIncludes(vertexPath, 0, includedPaths_[0]), vertexPath);
    const unsigned int geometryShader = compile(
        GL_GEOMETRY_SHADER, expandIncludes(geometryPath, 0, includedPaths_[1]), geometryPath);
    const unsigned int fragmentShader = compile(
        GL_FRAGMENT_SHADER, expandIncludes(fragmentPath, 0, includedPaths_[2]), fragmentPath);

    program_ = glCreateProgram();
    glAttachShader(program_, vertexShader);
    glAttachShader(program_, geometryShader);
    glAttachShader(program_, fragmentShader);
    glLinkProgram(program_);
    int linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        int logLength = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(static_cast<std::size_t>(std::max(logLength, 1)));
        glGetProgramInfoLog(program_, logLength, nullptr, log.data());
        glDeleteProgram(program_);
        program_ = 0;
        glDeleteShader(vertexShader);
        glDeleteShader(geometryShader);
        glDeleteShader(fragmentShader);
        throw std::runtime_error("Shader link failed:\n" + std::string(log.data()));
    }
    glDetachShader(program_, vertexShader);
    glDetachShader(program_, geometryShader);
    glDetachShader(program_, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(geometryShader);
    glDeleteShader(fragmentShader);
    captureWriteTimes();
    shaderRegistry().push_back(this);
}

Shader::~Shader() {
    auto& registry = shaderRegistry();
    registry.erase(std::remove(registry.begin(), registry.end(), this), registry.end());
    if (program_ != 0U) {
        glDeleteProgram(program_);
    }
}

Shader::ReloadReport Shader::reloadChangedShaders() {
    ReloadReport report;
    const std::vector<Shader*> shaders = shaderRegistry();
    for (Shader* shader : shaders) {
        if (shader == nullptr) continue;
        std::string error;
        if (shader->reloadIfChanged(error)) {
            ++report.reloaded;
        } else if (!error.empty()) {
            ++report.failed;
            if (!report.message.empty()) report.message += '\n';
            report.message += error;
        }
    }
    return report;
}

bool Shader::stageChanged(std::size_t stage) const {
    if (stage >= stagePaths_.size()) return false;
    // A shared header changing has to reload the shaders that include it, not only the stage file
    // that was edited. Without this the include expansion would silently break hot reload for every
    // shader that shares code -- which is the feature the expansion exists to enable.
    for (std::size_t index = 0; index < includedPaths_[stage].size(); ++index) {
        const auto recorded = index < includedWriteTimes_[stage].size()
            ? includedWriteTimes_[stage][index]
            : std::filesystem::file_time_type::min();
        if (shaderWriteTime(includedPaths_[stage][index]) != recorded) return true;
    }
    return false;
}

bool Shader::reloadIfChanged(std::string& error) {
    bool changed = false;
    for (std::size_t index = 0; index < stagePaths_.size(); ++index) {
        if (stagePaths_[index].empty()) continue;
        changed |= shaderWriteTime(stagePaths_[index]) != writeTimes_[index];
        changed |= stageChanged(index);
    }
    if (!changed) return false;

    try {
        if (hasGeometryStage_) {
            Shader candidate(stagePaths_[0], stagePaths_[1], stagePaths_[2]);
            std::swap(program_, candidate.program_);
        } else {
            Shader candidate(stagePaths_[0], stagePaths_[2]);
            std::swap(program_, candidate.program_);
        }
        captureWriteTimes();
        return true;
    } catch (const std::exception& exception) {
        captureWriteTimes();
        error = exception.what();
        return false;
    }
}

void Shader::captureWriteTimes() {
    for (std::size_t index = 0; index < stagePaths_.size(); ++index) {
        writeTimes_[index] = stagePaths_[index].empty()
            ? std::filesystem::file_time_type::min()
            : shaderWriteTime(stagePaths_[index]);
        includedWriteTimes_[index].clear();
        includedWriteTimes_[index].reserve(includedPaths_[index].size());
        for (const std::filesystem::path& dependency : includedPaths_[index]) {
            includedWriteTimes_[index].push_back(shaderWriteTime(dependency));
        }
    }
}

void Shader::use() const {
    glUseProgram(program_);
}

void Shader::setBool(const char* name, bool value) const {
    glUniform1i(uniformLocation(name), value ? 1 : 0);
}

void Shader::setInt(const char* name, int value) const {
    glUniform1i(uniformLocation(name), value);
}

void Shader::setFloat(const char* name, float value) const {
    glUniform1f(uniformLocation(name), value);
}

void Shader::setFloatByName(const char* name, float value) const {
    // `uniformLocation` returns -1 for a uniform the compiler removed, and glUniform with -1 is a
    // defined no-op, so an unused uniform does not have to be special-cased by the caller.
    glUniform1f(glGetUniformLocation(program_, name), value);
}

void Shader::setVec3(const char* name, const glm::vec3& value) const {
    glUniform3fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec2(const char* name, const glm::vec2& value) const {
    // These setters are not interchangeable. `glUniform2fv` reads two floats, so uploading a vec3
    // into a vec2 uniform does not fail -- it fills the uniform from the two floats it was given and
    // silently discards the third, which is a plausible-looking wrong value rather than an error.
    // The cloud field parity test spent a long time chasing exactly that: a wind vector uploaded with
    // setVec3 into a `vec2` uniform left its second component at whatever the driver had there.
    glUniform2fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec4(const char* name, const glm::vec4& value) const {
    glUniform4fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec4Array(
    const char* name,
    const glm::vec4* values,
    std::size_t count
) const {
    if (values == nullptr || count == 0U) return;
    glUniform4fv(
        uniformLocation(name),
        static_cast<GLsizei>(count),
        glm::value_ptr(values[0])
    );
}

void Shader::setFloatArray(const char* name, const float* values, std::size_t count) const {
    if (values == nullptr || count == 0U) return;
    glUniform1fv(uniformLocation(name), static_cast<GLsizei>(count), values);
}

void Shader::setIntArray(const char* name, const int* values, std::size_t count) const {
    if (values == nullptr || count == 0U) return;
    glUniform1iv(uniformLocation(name), static_cast<GLsizei>(count), values);
}

void Shader::setMat4(const char* name, const glm::mat4& value) const {
    glUniformMatrix4fv(uniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setMat4Array(const char* name, const glm::mat4* values, std::size_t count) const {
    if (values == nullptr || count == 0U) return;
    glUniformMatrix4fv(
        uniformLocation(name),
        static_cast<GLsizei>(count),
        GL_FALSE,
        glm::value_ptr(values[0])
    );
}

std::string Shader::readFile(const std::filesystem::path& path) {
    capture::recordInput(path);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open shader file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

unsigned int Shader::compile(unsigned int type, const std::string& source, const std::filesystem::path& path) {
    const unsigned int shader = glCreateShader(type);
    const char* sourcePointer = source.c_str();
    glShaderSource(shader, 1, &sourcePointer, nullptr);
    glCompileShader(shader);

    int compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        int logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(static_cast<std::size_t>(std::max(logLength, 1)));
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Shader compile failed (" + path.string() + "):\n" + log.data());
    }
    return shader;
}

int Shader::uniformLocation(const char* name) const {
    return glGetUniformLocation(program_, name);
}
