#pragma once
#include <filesystem>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace iris {
inline std::filesystem::path findRuntimeRoot() {
    std::vector<std::filesystem::path> candidates;
#ifdef _WIN32
    std::wstring executablePath(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr,
        executablePath.data(),
        static_cast<DWORD>(executablePath.size())
    );
    if (length > 0 && length < executablePath.size()) {
        executablePath.resize(length);
        candidates.push_back(std::filesystem::path(executablePath).parent_path());
    }
#endif
    std::error_code error;
    candidates.push_back(std::filesystem::current_path(error));
    candidates.emplace_back(MYRENDERER_SOURCE_DIR);
    for (const std::filesystem::path& candidate : candidates) {
        if (!candidate.empty()
            && std::filesystem::is_directory(candidate / "shaders", error)
            && std::filesystem::is_directory(candidate / "assets", error)) {
            return std::filesystem::absolute(candidate, error).lexically_normal();
        }
    }
    return std::filesystem::path(MYRENDERER_SOURCE_DIR);
}

}
