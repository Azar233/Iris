#include "app/FileDialog.h"

#include <algorithm>
#include <array>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

std::optional<std::filesystem::path> openModelFileDialog(std::string& error) {
    error.clear();
#ifdef _WIN32
    std::array<wchar_t, 32768> filePath{};
    static constexpr wchar_t filter[] =
        L"3D models (*.obj;*.dae;*.gltf;*.glb)\0*.obj;*.dae;*.gltf;*.glb\0"
        L"OBJ model (*.obj)\0*.obj\0"
        L"COLLADA model (*.dae)\0*.dae\0"
        L"glTF model (*.gltf;*.glb)\0*.gltf;*.glb\0"
        L"All files (*.*)\0*.*\0\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filePath.data();
    dialog.nMaxFile = static_cast<DWORD>(filePath.size());
    dialog.lpstrTitle = L"Open 3D model";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) != FALSE) {
        return std::filesystem::path(filePath.data());
    }

    const DWORD code = CommDlgExtendedError();
    if (code != 0U) {
        error = "The Windows file picker failed with code " + std::to_string(code);
    }
    return std::nullopt;
#else
    error = "The native model file picker is currently available on Windows only.";
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path> openSceneFileDialog(std::string& error) {
    error.clear();
#ifdef _WIN32
    std::array<wchar_t, 32768> filePath{};
    static constexpr wchar_t filter[] =
        L"Iris scene (*.myscene)\0*.myscene\0"
        L"All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filePath.data();
    dialog.nMaxFile = static_cast<DWORD>(filePath.size());
    dialog.lpstrTitle = L"Open Iris scene";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) != FALSE) return std::filesystem::path(filePath.data());
    const DWORD code = CommDlgExtendedError();
    if (code != 0U) error = "The Windows file picker failed with code " + std::to_string(code);
    return std::nullopt;
#else
    error = "The native scene file picker is currently available on Windows only.";
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path> openRenderJobFileDialog(std::string& error) {
    error.clear();
#ifdef _WIN32
    std::array<wchar_t, 32768> filePath{};
    static constexpr wchar_t filter[] =
        L"Iris Render Job (*.renderjob)\0*.renderjob\0"
        L"All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filePath.data();
    dialog.nMaxFile = static_cast<DWORD>(filePath.size());
    dialog.lpstrTitle = L"Open Iris Render Job";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) != FALSE) return std::filesystem::path(filePath.data());
    const DWORD code = CommDlgExtendedError();
    if (code != 0U) error = "The Windows file picker failed with code " + std::to_string(code);
    return std::nullopt;
#else
    error = "The native Render Job file picker is currently available on Windows only.";
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path> saveSceneFileDialog(
    const std::filesystem::path& suggestedPath,
    std::string& error
) {
    error.clear();
#ifdef _WIN32
    std::array<wchar_t, 32768> filePath{};
    const std::wstring suggested = suggestedPath.wstring();
    std::copy_n(suggested.c_str(), std::min(suggested.size(), filePath.size() - 1U), filePath.data());
    static constexpr wchar_t filter[] =
        L"Iris scene (*.myscene)\0*.myscene\0"
        L"All files (*.*)\0*.*\0\0";
    static constexpr wchar_t extension[] = L"myscene";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filePath.data();
    dialog.nMaxFile = static_cast<DWORD>(filePath.size());
    dialog.lpstrTitle = L"Save Iris scene";
    dialog.lpstrDefExt = extension;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog) != FALSE) return std::filesystem::path(filePath.data());
    const DWORD code = CommDlgExtendedError();
    if (code != 0U) error = "The Windows file picker failed with code " + std::to_string(code);
    return std::nullopt;
#else
    (void)suggestedPath;
    error = "The native scene file picker is currently available on Windows only.";
    return std::nullopt;
#endif
}
