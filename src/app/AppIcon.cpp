#include "app/AppIcon.h"

#include <array>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#else
#include <GLFW/glfw3.h>
#endif

#include <stb_image.h>

namespace {

constexpr const char* kIconRelativePath = "assets/icons/iris-icon.png";

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr,
        path.data(),
        static_cast<DWORD>(path.size())
    );
    if (length > 0 && length < path.size()) {
        path.resize(length);
        return std::filesystem::path(path).parent_path();
    }
#endif
    return {};
}

std::filesystem::path findIconPath() {
    std::error_code error;
    const std::array candidates{
        executableDirectory() / kIconRelativePath,
        std::filesystem::current_path(error) / kIconRelativePath,
        std::filesystem::path(MYRENDERER_SOURCE_DIR) / kIconRelativePath
    };
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
        error.clear();
    }
    return {};
}

#ifdef _WIN32
constexpr int kApplicationIconResource = 101;

void setNativeWindowsIcons(GLFWwindow* window) {
    HWND handle = glfwGetWin32Window(window);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (handle == nullptr || instance == nullptr) {
        return;
    }

    const auto loadIcon = [instance](int width, int height) {
        return static_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(kApplicationIconResource),
            IMAGE_ICON,
            width,
            height,
            LR_DEFAULTCOLOR | LR_SHARED
        ));
    };

    if (HICON small = loadIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))) {
        SendMessageW(handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
        SendMessageW(handle, WM_SETICON, ICON_SMALL2, reinterpret_cast<LPARAM>(small));
    }
    if (HICON large = loadIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))) {
        SendMessageW(handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
    }
}
#endif

} // namespace

void initializeMyRendererApplicationIdentity() {
#ifdef _WIN32
    using SetAppId = HRESULT(WINAPI*)(PCWSTR);
    bool releaseLibrary = false;
    HMODULE shell = GetModuleHandleW(L"shell32.dll");
    if (shell == nullptr) {
        shell = LoadLibraryW(L"shell32.dll");
        releaseLibrary = shell != nullptr;
    }
    if (shell != nullptr) {
        const FARPROC procedure = GetProcAddress(shell, "SetCurrentProcessExplicitAppUserModelID");
        SetAppId setAppId = nullptr;
        static_assert(sizeof(setAppId) == sizeof(procedure));
        std::memcpy(&setAppId, &procedure, sizeof(setAppId));
        if (setAppId != nullptr) {
            setAppId(L"Azar233.Iris");
        }
        if (releaseLibrary) {
            FreeLibrary(shell);
        }
    }
#endif
}

void setMyRendererWindowIcon(GLFWwindow* window) {
    if (window == nullptr) {
        return;
    }

    const std::filesystem::path iconPath = findIconPath();
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = iconPath.empty()
        ? nullptr
        : stbi_load(iconPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels != nullptr) {
        const GLFWimage image{width, height, pixels};
        glfwSetWindowIcon(window, 1, &image);
        stbi_image_free(pixels);
    }

#ifdef _WIN32
    // Applying the compiled multi-size resource as well prevents Windows from
    // falling back to the generic icon in the taskbar and Alt+Tab switcher.
    setNativeWindowsIcons(window);
#endif
}
