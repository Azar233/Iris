#include "asset/InputManifest.h"
#include "render/Shader.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

namespace {

std::vector<Shader*>& shaderRegistry() {
    static std::vector<Shader*> registry;
    return registry;
}

std::size_t& shaderRegistryRevision() { static std::size_t revision = 0; return revision; }

// Also owns stages compiled before a later stage or link fails.
struct CompilationResources {
    CompilationResources() = default;
    CompilationResources(const CompilationResources&) = delete;
    CompilationResources& operator=(const CompilationResources&) = delete;
    unsigned int program{0};
    std::array<unsigned int, 3> stages{};
    ~CompilationResources() {
        for (auto stage : stages) if (stage) glDeleteShader(stage);
        if (program) glDeleteProgram(program);
    }
};

std::filesystem::file_time_type shaderWriteTime(const std::filesystem::path& path) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    return error ? std::filesystem::file_time_type::min() : time;
}

void watchSource(const std::filesystem::path& path, std::vector<std::filesystem::path>& paths,
    std::vector<std::filesystem::file_time_type>& times) {
    if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
        paths.push_back(path);
        times.push_back(shaderWriteTime(path));
    }
}

} // namespace

std::string Shader::expandIncludes(
    const std::filesystem::path& path,
    int depth,
    std::vector<std::filesystem::path>& dependencies,
    std::vector<std::filesystem::file_time_type>& times
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
    watchSource(path, dependencies, times);
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
        watchSource(resolved, dependencies, times);
        if (!std::filesystem::is_regular_file(resolved)) {
            constexpr int maximumAncestors = 6;
            std::filesystem::path directory = path.parent_path();
            for (int level = 0; level < maximumAncestors; ++level) {
                const std::filesystem::path candidate = directory / requested;
                watchSource(candidate, dependencies, times);
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
        output << expandIncludes(resolved, depth + 1, dependencies, times);
        output << "#line 1 \"" << path.generic_string() << "\"\n";
    }
    return output.str();
}

Shader::Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath)
    : Shader(std::array<std::filesystem::path, 3>{vertexPath, {}, fragmentPath}) {}

Shader::Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& geometryPath,
    const std::filesystem::path& fragmentPath)
    : Shader(std::array<std::filesystem::path, 3>{vertexPath, geometryPath, fragmentPath}) {}

Shader::Shader(std::array<std::filesystem::path, 3> paths) : stagePaths_(std::move(paths)) {
    CompilationResources resources;
    resources.program = buildProgram(stagePaths_, watchedSources_);
    if (snapshotChanged(watchedSources_)) throw std::runtime_error("Shader sources changed during construction");
    shaderRegistry().push_back(this);
    ++shaderRegistryRevision();
    program_ = resources.program;
    resources.program = 0;
}

unsigned int Shader::buildProgram(const std::array<std::filesystem::path, 3>& paths, SourceSnapshot& snapshot) {
    // Capture times before reading, then recheck before publishing GPU programs.
    for (std::size_t i = 0; i < paths.size(); ++i)
        if (!paths[i].empty()) watchSource(paths[i], snapshot.paths[i], snapshot.times[i]);
    std::array<std::string, 3> sources;
    for (std::size_t i = 0; i < paths.size(); ++i)
        if (!paths[i].empty()) sources[i] = expandIncludes(paths[i], 0, snapshot.paths[i], snapshot.times[i]);
    CompilationResources resources;
    const std::array<unsigned int, 3> types{GL_VERTEX_SHADER, GL_GEOMETRY_SHADER, GL_FRAGMENT_SHADER};
    for (std::size_t i = 0; i < paths.size(); ++i)
        if (!paths[i].empty()) resources.stages[i] = compile(types[i], sources[i], paths[i]);
    resources.program = glCreateProgram();
    if (!resources.program) throw std::runtime_error("Cannot allocate Shader program");
    for (auto stage : resources.stages) if (stage) glAttachShader(resources.program, stage);
    glLinkProgram(resources.program);
    int linked = GL_FALSE;
    glGetProgramiv(resources.program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        int length = 0;
        glGetProgramiv(resources.program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)));
        glGetProgramInfoLog(resources.program, length, nullptr, log.data());
        throw std::runtime_error("Shader link failed (" + paths[2].string() + "):\n" + log.data());
    }
    for (auto stage : resources.stages) if (stage) glDetachShader(resources.program, stage);
    const auto program = resources.program;
    resources.program = 0;
    return program;
}

Shader::~Shader() {
    auto& registry = shaderRegistry();
    registry.erase(std::remove(registry.begin(), registry.end(), this), registry.end());
    ++shaderRegistryRevision();
    if (program_) glDeleteProgram(program_);
}

bool Shader::snapshotChanged(const SourceSnapshot& snapshot) {
    for (std::size_t stage = 0; stage < snapshot.paths.size(); ++stage) {
        if (snapshot.paths[stage].size() != snapshot.times[stage].size()) return true;
        for (std::size_t i = 0; i < snapshot.paths[stage].size(); ++i)
            if (shaderWriteTime(snapshot.paths[stage][i]) != snapshot.times[stage][i]) return true;
    }
    return false;
}

Shader::ReloadReport Shader::reloadChangedShaders(bool retryPending) {
    ReloadReport report;
    const auto shaders = shaderRegistry();
    static std::size_t failedRegistryRevision = 0;
    bool changed = false, pending = false;
    for (const auto* shader : shaders) {
        changed = changed || snapshotChanged(shader->watchedSources_);
        pending = pending || shader->reloadPending_;
        if (shader->reloadPending_) ++report.pending;
    }
    if (!changed && !(pending && (retryPending || failedRegistryRevision != shaderRegistryRevision()))) return report;
    struct Candidate {
        Shader* target;
        unsigned int program{0};
        SourceSnapshot sources;
        explicit Candidate(Shader* shader) : target(shader) {}
        ~Candidate() { if (program) glDeleteProgram(program); }
    };
    std::vector<std::unique_ptr<Candidate>> candidates;
    for (auto* shader : shaders) {
        if (!shader->reloadPending_ && !snapshotChanged(shader->watchedSources_)) continue;
        auto candidate = std::make_unique<Candidate>(shader);
        try {
            candidate->program = buildProgram(shader->stagePaths_, candidate->sources);
        } catch (const std::exception& error) {
            ++report.failed;
            if (!report.message.empty()) report.message += '\n';
            report.message += error.what();
        }
        candidates.push_back(std::move(candidate));
    }
    // No allocation or file reads occur during publication; a changed input rejects
    // the complete batch just like a compiler error.
    for (const auto& candidate : candidates) {
        if (snapshotChanged(candidate->sources)) {
            ++report.failed;
            report.message += "\nShader sources changed while preparing: " + candidate->target->stagePaths_[2].string();
        }
    }
    if (report.failed) {
        report.retained = candidates.size();
        report.pending = candidates.size();
        report.message = "Reload transaction rejected; retained " + std::to_string(report.retained)
            + " program(s).\n" + report.message;
        for (auto& candidate : candidates) {
            candidate->target->watchedSources_ = std::move(candidate->sources);
            candidate->target->reloadPending_ = true;
        }
        failedRegistryRevision = shaderRegistryRevision();
        return report; // Candidate destructors release every uncommitted program.
    }
    for (auto& candidate : candidates) {
        std::swap(candidate->program, candidate->target->program_);
        std::swap(candidate->sources, candidate->target->watchedSources_);
        candidate->target->reloadPending_ = false;
    }
    report.reloaded = candidates.size();
    report.pending = 0;
    return report;
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
    CompilationResources resources;
    const unsigned int shader = resources.stages[0] = glCreateShader(type);
    if (!shader) throw std::runtime_error("Cannot allocate Shader stage: " + path.string());
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
        throw std::runtime_error("Shader compile failed (" + path.string() + "):\n" + log.data());
    }
    resources.stages[0] = 0;
    return shader;
}

int Shader::uniformLocation(const char* name) const {
    return glGetUniformLocation(program_, name);
}
