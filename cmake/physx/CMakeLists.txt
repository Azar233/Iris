# Keep SDK compiler flags and output paths in this directory, away from Iris.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(PX_BUILDSNIPPETS OFF)
set(PX_BUILDPVDRUNTIME OFF)
set(PX_GENERATE_GPU_PROJECTS OFF)
set(PX_GENERATE_GPU_PROJECTS_ONLY OFF)
set(PX_GENERATE_STATIC_LIBRARIES ON)
set(PX_FLOAT_POINT_PRECISE_MATH ON)
set(NV_USE_STATIC_WINCRT OFF)
set(NV_USE_DEBUG_WINCRT ON)
set(TARGET_BUILD_PLATFORM windows)
set(PX_OUTPUT_LIB_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(PX_OUTPUT_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}")

FetchContent_Declare(iris_physx
    URL https://codeload.github.com/NVIDIA-Omniverse/PhysX/zip/5ca9f472105a90d70d957c243cb0ef36fe251a9f
    URL_HASH SHA256=b20b5c497ae51e6556519323a999447adf1c24a8b6265ecaee23ac40fb62be12
    SOURCE_SUBDIR physx
)
# The upstream project requires its source root before adding its CMake directory.
FetchContent_MakeAvailable(iris_physx)
set(PHYSX_ROOT_DIR "${iris_physx_SOURCE_DIR}/physx")
# 5.6.1 copies FreeGLUT even with snippets disabled. Gate that optional copy by
# its explicit switch, retaining PUBLIC_RELEASE and all solver source unchanged.
set(iris_physx_windows "${PHYSX_ROOT_DIR}/source/compiler/cmake/windows/CMakeLists.txt")
file(READ "${iris_physx_windows}" iris_physx_windows_text)
if(iris_physx_windows_text MATCHES "IF\\(PX_COPY_EXTERNAL_DLL OR PUBLIC_RELEASE\\)")
    string(REPLACE "IF(PX_COPY_EXTERNAL_DLL OR PUBLIC_RELEASE)" "IF(PX_COPY_EXTERNAL_DLL)"
        iris_physx_windows_text "${iris_physx_windows_text}")
    file(WRITE "${iris_physx_windows}" "${iris_physx_windows_text}")
elseif(NOT iris_physx_windows_text MATCHES "IF\\(PX_COPY_EXTERNAL_DLL\\)")
    message(FATAL_ERROR "PhysX FreeGLUT copy guard changed; review the SDK adapter.")
endif()
set(PX_COPY_EXTERNAL_DLL OFF)
add_subdirectory("${PHYSX_ROOT_DIR}/compiler/public" "${iris_physx_BINARY_DIR}" EXCLUDE_FROM_ALL)

add_library(IrisPhysicsSdk INTERFACE)
target_include_directories(IrisPhysicsSdk SYSTEM INTERFACE "${PHYSX_ROOT_DIR}/include")
target_compile_definitions(IrisPhysicsSdk INTERFACE PX_PHYSX_STATIC_LIB)
target_link_libraries(IrisPhysicsSdk INTERFACE PhysX PhysXCommon PhysXFoundation PhysXExtensions PhysXPvdSDK)
install(FILES "${iris_physx_SOURCE_DIR}/LICENSE.md" DESTINATION licenses RENAME PhysX-LICENSE.md COMPONENT Runtime)
