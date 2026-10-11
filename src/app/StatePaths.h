#pragma once
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace iris {
inline std::filesystem::path stateDirectory() {
    if (const auto* value = std::getenv("IRIS_STATE_DIRECTORY"))
        if (*value) return std::filesystem::absolute(std::filesystem::u8path(value));
#ifdef _WIN32
    if (const auto* value = std::getenv("LOCALAPPDATA"))
        if (*value) return std::filesystem::u8path(value) / "Iris";
#endif
    return std::filesystem::temp_directory_path() / "Iris";
}
// Copy a legacy preference once; an existing Iris preference always wins.
inline std::filesystem::path migrateStateFile(const std::filesystem::path& directory,
    const std::filesystem::path& name, const std::filesystem::path& legacy = {}) {
    if (name.empty() || name.has_parent_path() || name == "." || name == "..")
        throw std::invalid_argument("State filename must be a leaf name");
    std::filesystem::create_directories(directory);
    const auto target = directory / name;
    if (!std::filesystem::exists(target) && !legacy.empty() && std::filesystem::is_regular_file(legacy))
        std::filesystem::copy_file(legacy, target, std::filesystem::copy_options::none);
    return target;
}
inline std::filesystem::path stateFile(const std::filesystem::path& name,
    const std::filesystem::path& legacy = {}) {
    return migrateStateFile(stateDirectory(), name, legacy);
}
} // namespace iris
