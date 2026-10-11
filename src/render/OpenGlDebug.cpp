#include "render/OpenGlDebug.h"

#include <iostream>
#include <cstring>

#include <glad/gl.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "third_party/renderdoc_app.h"
#endif

bool triggerRenderDocFrameCapture() {
#ifdef _WIN32
    const auto module = GetModuleHandleW(L"renderdoc.dll");
    if (!module) return false;
    const auto procedure = GetProcAddress(module, "RENDERDOC_GetAPI");
    pRENDERDOC_GetAPI getApi = nullptr;
    static_assert(sizeof(getApi) == sizeof(procedure), "Windows procedure pointer ABI mismatch");
    std::memcpy(&getApi, &procedure, sizeof(getApi));
    RENDERDOC_API_1_0_0* api = nullptr;
    if (!getApi || getApi(eRENDERDOC_API_Version_1_0_0, reinterpret_cast<void**>(&api)) != 1 || !api) return false;
    api->TriggerCapture();
    return true;
#else
    return false;
#endif
}

#ifndef NDEBUG
namespace {

const char* debugSourceName(unsigned int source) {
    switch (source) {
    case GL_DEBUG_SOURCE_API: return "API";
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "window system";
    case GL_DEBUG_SOURCE_SHADER_COMPILER: return "shader compiler";
    case GL_DEBUG_SOURCE_THIRD_PARTY: return "third party";
    case GL_DEBUG_SOURCE_APPLICATION: return "application";
    case GL_DEBUG_SOURCE_OTHER: return "other";
    default: return "unknown source";
    }
}

const char* debugTypeName(unsigned int type) {
    switch (type) {
    case GL_DEBUG_TYPE_ERROR: return "error";
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "deprecated behavior";
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "undefined behavior";
    case GL_DEBUG_TYPE_PORTABILITY: return "portability";
    case GL_DEBUG_TYPE_PERFORMANCE: return "performance";
    case GL_DEBUG_TYPE_MARKER: return "marker";
    case GL_DEBUG_TYPE_PUSH_GROUP: return "push group";
    case GL_DEBUG_TYPE_POP_GROUP: return "pop group";
    case GL_DEBUG_TYPE_OTHER: return "other";
    default: return "unknown type";
    }
}

const char* debugSeverityName(unsigned int severity) {
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH: return "high";
    case GL_DEBUG_SEVERITY_MEDIUM: return "medium";
    case GL_DEBUG_SEVERITY_LOW: return "low";
    case GL_DEBUG_SEVERITY_NOTIFICATION: return "notification";
    default: return "unknown severity";
    }
}

void GLAPIENTRY openGlDebugCallback(
    unsigned int source,
    unsigned int type,
    unsigned int id,
    unsigned int severity,
    int,
    const char* message,
    const void*
) {
    std::cerr << "OpenGL debug [" << debugSeverityName(severity) << "] "
              << debugTypeName(type) << " from " << debugSourceName(source)
              << " (id " << id << "): " << (message == nullptr ? "No message" : message) << '\n';
}

} // namespace
#endif

void initializeOpenGlDebugOutput() {
#ifndef NDEBUG
    if (GLAD_GL_KHR_debug == 0 || glDebugMessageCallback == nullptr) {
        std::cout << "OpenGL debug output: KHR_debug is unavailable\n";
        return;
    }

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(openGlDebugCallback, nullptr);
    glDebugMessageControl(
        GL_DONT_CARE,
        GL_DONT_CARE,
        GL_DEBUG_SEVERITY_NOTIFICATION,
        0,
        nullptr,
        GL_FALSE
    );
    std::cout << "OpenGL debug output: enabled\n";
#endif
}
