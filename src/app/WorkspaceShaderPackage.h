#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct WorkspaceShaderFile {
    std::filesystem::path path;
    std::string role;
    std::vector<std::string> owners;
    std::uintmax_t sizeBytes{0};
    bool missing{false};
    bool executionInput{true};
};
struct WorkspaceShaderPackage {
    bool manifestReadable{false}; // Header/root parsed; dependency diagnostics may still exist.
    std::filesystem::path manifest, root;
    std::string id, author, license;
    std::vector<WorkspaceShaderFile> files;
    std::vector<std::string> generatedInputs;
    std::vector<std::string> diagnostics;
};
// Read-only inspection, including broken inputs. This is not runtime validation
// and never compiles shaders, decodes textures or modifies the live renderer.
WorkspaceShaderPackage inspectWorkspaceShaderPackage(const std::filesystem::path& manifest);
