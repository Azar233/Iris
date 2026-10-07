#include "app/AppIcon.h"

#include <array>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#else
#include <GLFW/glfw3.h>
#endif

#include "asset/StbImageCompat.h"

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

    if (HICON smallIcon = loadIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))) {
        SendMessageW(handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        SendMessageW(handle, WM_SETICON, ICON_SMALL2, reinterpret_cast<LPARAM>(smallIcon));
    }
    if (HICON large = loadIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))) {
        SendMessageW(handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
    }

    // Taskbar groups use Shell properties independently of WM_SETICON. Point
    // directly at the embedded resource so portable builds need no shortcut.
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IPropertyStore* properties = nullptr;
    const HRESULT storeResult = SHGetPropertyStoreForWindow(handle, IID_PPV_ARGS(&properties));
    if (SUCCEEDED(storeResult)) {
        const auto setString = [properties](REFPROPERTYKEY key, const std::wstring& text) {
            PROPVARIANT value{};
            value.vt = VT_LPWSTR;
            value.pwszVal = const_cast<wchar_t*>(text.c_str());
            return properties->SetValue(key, value);
        };
        std::wstring executable(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
                                                static_cast<DWORD>(executable.size()));
        if (length > 0 && length < executable.size()) {
            executable.resize(length);
            const HRESULT commandResult = setString(PKEY_AppUserModel_RelaunchCommand, L"\"" + executable + L"\"");
            const HRESULT nameResult = setString(PKEY_AppUserModel_RelaunchDisplayNameResource, L"Iris");
            const HRESULT iconResult = setString(PKEY_AppUserModel_RelaunchIconResource,
                      executable + L",-" + std::to_wstring(kApplicationIconResource));
            // Set identity last, once all taskbar presentation properties exist.
            const HRESULT idResult = setString(PKEY_AppUserModel_ID, L"Azar233.Iris");
            const HRESULT commitResult = properties->Commit();
            if (FAILED(commandResult) || FAILED(nameResult) || FAILED(iconResult)
                || FAILED(idResult) || FAILED(commitResult)) {
                std::cerr << "Failed to configure Iris taskbar properties\n";
            } else if (std::getenv("MYRENDERER_SMOKE_TEST") != nullptr) {
                std::cout << "Iris taskbar identity and embedded icon configured\n";
            }
        }
        properties->Release();
    } else {
        std::cerr << "Failed to access Iris taskbar properties: " << storeResult << '\n';
    }
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
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

void clearMyRendererWindowIdentity(GLFWwindow* window) {
#ifdef _WIN32
    if (window == nullptr) {
        return;
    }
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IPropertyStore* properties = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(glfwGetWin32Window(window),
                                             IID_PPV_ARGS(&properties)))) {
        const PROPVARIANT empty{};
        for (const PROPERTYKEY* key : {&PKEY_AppUserModel_ID,
                                       &PKEY_AppUserModel_RelaunchCommand,
                                       &PKEY_AppUserModel_RelaunchDisplayNameResource,
                                       &PKEY_AppUserModel_RelaunchIconResource}) {
            properties->SetValue(*key, empty);
        }
        properties->Release();
    }
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
#else
    (void)window;
#endif
}
