#pragma once

#include <filesystem>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

class Shader {
public:
    struct ReloadReport {
        std::size_t reloaded{0U};
        std::size_t failed{0U};
        std::string message;
        std::size_t retained{0U};
        std::size_t pending{0U};
    };

    Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
    Shader(
        const std::filesystem::path& vertexPath,
        const std::filesystem::path& geometryPath,
        const std::filesystem::path& fragmentPath
    );
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void use() const;
    bool sourcesCurrent() const { return !snapshotChanged(watchedSources_); }
    void setBool(const char* name, bool value) const;
    void setInt(const char* name, int value) const;
    void setFloat(const char* name, float value) const;
    void setVec3(const char* name, const glm::vec3& value) const;
    void setVec2(const char* name, const glm::vec2& value) const;
    void setVec4(const char* name, const glm::vec4& value) const;
    void setVec4Array(const char* name, const glm::vec4* values, std::size_t count) const;
    void setFloatArray(const char* name, const float* values, std::size_t count) const;
    void setIntArray(const char* name, const int* values, std::size_t count) const;
    void setMat4(const char* name, const glm::mat4& value) const;
    void setMat4Array(const char* name, const glm::mat4* values, std::size_t count) const;
    // Looks a uniform up at call time and ignores it when the compiler removed it (location -1).
    // This is the only way to drive a *dynamic* uniform name, which a parity test needs: it has to
    // ask the CPU for the value the shader is about to compute at each sample, and it cannot know
    // that name in advance.
    void setFloatByName(const char* name, float value) const;
    // One context-thread transaction for every changed/pending registered program.
    static ReloadReport reloadChangedShaders(bool retryPending = false);

private:
    struct SourceSnapshot {
        std::array<std::vector<std::filesystem::path>, 3> paths;
        std::array<std::vector<std::filesystem::file_time_type>, 3> times;
    };
    explicit Shader(std::array<std::filesystem::path, 3> paths);
    static unsigned int buildProgram(const std::array<std::filesystem::path, 3>& paths, SourceSnapshot& snapshot);
    static bool snapshotChanged(const SourceSnapshot& snapshot);
    static std::string readFile(const std::filesystem::path& path);
    // Resolves `#include "name"` relative to the including file and inlines the result, recording
    // every file it touched. GLSL 3.30 has no include of its own, and expanding it here is what lets
    // a shader and the C++ code that must agree with it read the same file.
    static std::string expandIncludes(
        const std::filesystem::path& path,
        int depth,
        std::vector<std::filesystem::path>& dependencies,
        std::vector<std::filesystem::file_time_type>& times
    );
    static unsigned int compile(unsigned int type, const std::string& source, const std::filesystem::path& path);
    int uniformLocation(const char* name) const;

    unsigned int program_{0};
    std::array<std::filesystem::path, 3> stagePaths_{};
    SourceSnapshot watchedSources_;
    bool reloadPending_{false};
};
