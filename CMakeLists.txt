cmake_minimum_required(VERSION 3.20)

project(Iris VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# tinyobjloader v1.0.6 predates modern CMake policy declarations. This keeps
# CMake 4.x compatible without weakening this project's own minimum version.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

# Build stamp reported by every module manifest. It is fixed for one configure, so
# the GUI and the batch CLI built together always agree, while a reconfigure cannot
# silently reuse a simulation cache produced by an older binary.
string(TIMESTAMP MYRENDERER_BUILD_STAMP "%Y%m%d%H%M%S" UTC)
set(MYRENDERER_BUILD_ID "${PROJECT_VERSION}+${MYRENDERER_BUILD_STAMP}")

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
)

set(TINYOBJLOADER_BUILD_TEST_LOADER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG v1.0.6
    GIT_SHALLOW TRUE
)

set(TINYEXR_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(TINYEXR_USE_MINIZ ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG v1.0.13
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.7-docking
    GIT_SHALLOW TRUE
)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v6.0.5
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw glm glad tinyobjloader tinyexr imgui assimp)
FetchContent_GetProperties(glad SOURCE_DIR MYRENDERER_GLAD_FETCH_SOURCE_DIR)

add_subdirectory(${glad_SOURCE_DIR}/cmake ${glad_BINARY_DIR}/cmake)
glad_add_library(glad_gl_core_33 REPRODUCIBLE API gl:core=3.3)

add_library(imgui_backend STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_backend PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(imgui_backend PRIVATE
    GLFW_INCLUDE_NONE
    IMGUI_IMPL_OPENGL_LOADER_GLAD2
)
target_link_libraries(imgui_backend PUBLIC glfw glad_gl_core_33)

# Offline cloud assets have no GL or editor dependency. Upload remains on the context thread.
add_library(MyRendererCloudNoise STATIC src/optics/CloudNoiseVolume.cpp src/optics/CloudLightingLut.cpp)
target_include_directories(MyRendererCloudNoise PUBLIC src)
target_compile_definitions(MyRendererCloudNoise PRIVATE MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(MyRendererCloudNoise PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MyRendererCloudNoise PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(MyRendererCloudLightingAsset tools/CloudLightingAsset.cpp)
target_link_libraries(MyRendererCloudLightingAsset PRIVATE MyRendererCloudNoise)
add_executable(MyRendererCloudNoiseAsset tools/CloudNoiseAsset.cpp)
target_link_libraries(MyRendererCloudNoiseAsset PRIVATE MyRendererCloudNoise)

add_library(MyRendererPathTracing STATIC
    src/optics/Atmosphere.cpp
    src/optics/CloudReference.cpp
    src/pathtracer/AovDenoiser.cpp
    src/pathtracer/Bvh.cpp
    src/pathtracer/InstancedBvh.cpp
    src/pathtracer/RayGeometry.cpp
    src/pathtracer/SceneSnapshot.cpp
    src/pathtracer/PbrBsdf.cpp
    src/pathtracer/MaterialBsdf.cpp
    src/pathtracer/TileScheduler.cpp
    src/pathtracer/TextureSampling.cpp
    src/pathtracer/EnvironmentSampling.cpp
    src/pathtracer/LightSampling.cpp
    src/pathtracer/ProgressiveRenderer.cpp
    src/pathtracer/AcceptanceScene.cpp
    src/pathtracer/ImageOutput.cpp
    src/pathtracer/ReferenceComparison.cpp
)
target_include_directories(MyRendererPathTracing PUBLIC src)
target_compile_definitions(MyRendererPathTracing PRIVATE
    MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
find_package(Threads REQUIRED)
target_link_libraries(MyRendererPathTracing PUBLIC glm::glm Threads::Threads MyRendererCloudNoise PRIVATE tinyexr zlibstatic)
target_include_directories(MyRendererPathTracing PRIVATE
    ${assimp_SOURCE_DIR}/contrib/stb
    ${assimp_SOURCE_DIR}/contrib/zlib
    ${assimp_BINARY_DIR}/contrib/zlib
    ${tinyexr_SOURCE_DIR}
)
add_executable(MyRendererReferenceRender tools/ReferenceRender.cpp)
target_link_libraries(MyRendererReferenceRender PRIVATE MyRendererPathTracing)
add_executable(MyRendererInstancingBenchmark tools/InstancingBenchmark.cpp)
target_link_libraries(MyRendererInstancingBenchmark PRIVATE MyRendererPathTracing)
add_executable(MyRendererAdaptiveSamplingBenchmark tools/AdaptiveSamplingBenchmark.cpp)
target_link_libraries(MyRendererAdaptiveSamplingBenchmark PRIVATE MyRendererPathTracing)
add_executable(MyRendererDenoisingExperiment tools/DenoisingExperiment.cpp)
target_link_libraries(MyRendererDenoisingExperiment PRIVATE MyRendererPathTracing)
# Cloud layer measurement: prints the sky and cloud radiance a scene's parameters produce, so the
# layer's lighting can be calibrated from numbers instead of from repeated image edits.
add_executable(MyRendererCloudCalibration tools/CloudCalibration.cpp)
target_link_libraries(MyRendererCloudCalibration PRIVATE MyRendererPathTracing)

add_library(MyRendererBatchRuntime STATIC
    src/runtime/BatchRuntime.cpp
    src/runtime/RenderQueue.cpp
    src/runtime/RenderJob.cpp # Raster schema 3 capture contract.
    src/runtime/RasterFrameReport.cpp
    src/asset/BuiltinModels.cpp
    src/io/AssimpImporter.cpp
    src/io/GltfMaterialExtensions.cpp
    src/io/ObjLoader.cpp
    src/pathtracer/SceneLighting.cpp
    src/render/Camera.cpp
    src/scene/Scene.cpp
    src/scene/SceneDocument.cpp
)
target_include_directories(MyRendererBatchRuntime PRIVATE
    src
    ${assimp_SOURCE_DIR}/contrib/rapidjson/include
)
target_compile_definitions(MyRendererBatchRuntime PRIVATE
    MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(MyRendererBatchRuntime PUBLIC MyRendererPathTracing PRIVATE
    glm::glm assimp::assimp tinyobjloader glad_gl_core_33
)
if(MSVC)
    target_compile_options(MyRendererBatchRuntime PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MyRendererBatchRuntime PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(MyRendererBatch tools/MyRendererBatch.cpp)
target_link_libraries(MyRendererBatch PRIVATE MyRendererBatchRuntime MyRendererModules)

# Statically linked module set (P1-0C). The parameter / registry / runtime-scene
# framework lives in this target together with the concrete modules, so adding a
# module does not touch the editor or the batch CLI. `Scene` symbols are provided by
# the consumer, exactly like the other shared sources in this project.
add_library(MyRendererModules STATIC
    src/module/BuiltinModules.cpp
    src/module/CoastalSequenceModule.cpp
    src/module/ModuleRegistry.cpp
    src/module/ModuleRuntime.cpp
    src/module/ParameterRegistry.cpp
    src/module/RuntimeScene.cpp
    src/module/SimulationCache.cpp
)
target_include_directories(MyRendererModules PUBLIC src)
target_include_directories(MyRendererModules PRIVATE
    ${assimp_SOURCE_DIR}/contrib/rapidjson/include
)
target_compile_definitions(MyRendererModules PRIVATE
    MYRENDERER_BUILD_ID="${MYRENDERER_BUILD_ID}"
)
target_link_libraries(MyRendererModules PUBLIC glm::glm PRIVATE glad_gl_core_33)
if(MSVC)
    target_compile_options(MyRendererModules PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MyRendererModules PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_custom_target(path-tracing-instancing-benchmark
    COMMAND MyRendererInstancingBenchmark
    DEPENDS MyRendererInstancingBenchmark
    COMMENT "Comparing world-space BVH with shared-geometry BLAS/TLAS"
)
add_custom_target(path-tracing-adaptive-benchmark
    COMMAND MyRendererAdaptiveSamplingBenchmark
    DEPENDS MyRendererAdaptiveSamplingBenchmark
    COMMENT "Measuring variance-guided adaptive sampling against uniform SPP"
)
add_custom_target(path-tracing-denoising-experiment
    COMMAND MyRendererDenoisingExperiment
            ${CMAKE_CURRENT_BINARY_DIR}/path-tracing-denoising 64 2048
    DEPENDS MyRendererDenoisingExperiment
    COMMENT "Capturing P0-D low-SPP AOV denoising evidence and metrics"
)
add_custom_target(path-tracing-acceptance
    COMMAND MyRendererReferenceRender ${CMAKE_CURRENT_BINARY_DIR}/path-tracing-acceptance/volume-glass
    DEPENDS MyRendererReferenceRender
)
if(MSVC)
    target_compile_options(MyRendererPathTracing PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MyRendererPathTracing PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(MyRenderer
    src/main.cpp
    src/app/AppIcon.cpp
    src/app/AssetThumbnail.cpp
    src/app/Application.cpp
    src/app/ApplicationScene.cpp
    src/app/EditorSession.cpp
    src/app/WorkspaceAssets.cpp
    src/asset/BuiltinModels.cpp
    src/app/FileDialog.cpp
    src/io/AssimpImporter.cpp
    src/io/GltfMaterialExtensions.cpp
    src/io/ObjLoader.cpp
    src/optics/Atmosphere.cpp
    src/optics/PrismOptics.cpp
    src/optics/PrismDemo.cpp
    src/optics/SpectralBeamMesh.cpp
    src/pathtracer/SceneSnapshotCapture.cpp
    src/pathtracer/SceneLighting.cpp
    src/render/Camera.cpp
    src/render/CausticsMap.cpp
    src/render/CloudLayerRenderer.cpp
    src/render/CloudShadowRenderer.cpp
    src/render/CloudNoiseTexture.cpp
    src/render/GodRaysRenderer.cpp
    src/render/DebugGrid.cpp
    src/render/EnvironmentMap.cpp
    src/render/GBuffer.cpp
    src/render/GpuModel.cpp
    src/render/Mesh.cpp
    src/render/OpenGlDebug.cpp
    src/render/OpenGlStateCache.cpp
    src/render/OpticalPathDebugRenderer.cpp
    src/render/PostProcessor.cpp
    src/render/RenderTarget.cpp
    src/render/Renderer.cpp
    src/render/EnscapeCubeRenderer.cpp
    src/render/SceneDrawList.cpp
    src/render/Shader.cpp
    src/render/ShadowMap.cpp
    src/render/ShadowCascade.cpp
    src/render/SsaoRenderer.cpp
    src/render/SpectralBeamRenderer.cpp
    src/render/Texture2D.cpp
    src/render/WaterSurface.cpp
    src/render/WaterWaves.cpp
    src/scene/Scene.cpp
    src/scene/SceneDocument.cpp
)

if(WIN32)
    enable_language(RC)
    set(MYRENDERER_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/iris.ico")
    configure_file(
        resources/MyRenderer.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/MyRenderer.rc"
        @ONLY
    )
    target_sources(MyRenderer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/MyRenderer.rc")
    target_link_libraries(MyRenderer PRIVATE comdlg32)
endif()

target_include_directories(MyRenderer PRIVATE
    src
    ${assimp_SOURCE_DIR}/contrib/stb
    ${assimp_SOURCE_DIR}/contrib/rapidjson/include
    ${assimp_SOURCE_DIR}/contrib/zlib
    ${assimp_BINARY_DIR}/contrib/zlib
    ${tinyexr_SOURCE_DIR}
)
target_compile_definitions(MyRenderer PRIVATE
    GLFW_INCLUDE_NONE
    MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(MyRenderer PRIVATE
    glad_gl_core_33
    glfw
    glm::glm
    imgui_backend
    assimp::assimp
    tinyobjloader
    tinyexr
    zlibstatic
    MyRendererPathTracing
    MyRendererBatchRuntime
    MyRendererModules
)

if(BUILD_TESTING)
    set(COASTAL_COMPARATOR "$<TARGET_FILE:MyRendererImageComparison>")
endif()
add_custom_target(coastal-sequence-acceptance
    COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DCOMPARATOR=${COASTAL_COMPARATOR}
            -DJOB=${CMAKE_CURRENT_SOURCE_DIR}/assets/renderjobs/04_coastal_sequence.renderjob
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/CoastalSequenceAcceptance.cmake
    DEPENDS MyRenderer
    COMMENT "Checking deterministic raster Render Job day-to-sunset sequence"
)

add_custom_target(path-tracing-raster-comparison
    COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/ReferenceComparisonAcceptance.cmake
    DEPENDS MyRenderer
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Capturing the three fixed-camera raster/path-traced/difference acceptance suites"
)

if(MSVC)
    target_compile_options(MyRenderer PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MyRenderer PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET MyRenderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:MyRenderer>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:MyRenderer>/assets
    COMMENT "Copying shaders and model assets"
)

if(BUILD_TESTING)
    add_executable(MyRendererRenderJobTests tests/RenderJobTests.cpp)
    target_link_libraries(MyRendererRenderJobTests PRIVATE MyRendererBatchRuntime MyRendererModules)
    target_compile_definitions(MyRendererRenderJobTests PRIVATE
        MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(MSVC)
        target_compile_options(MyRendererRenderJobTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererRenderJobTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME render-job-runtime COMMAND MyRendererRenderJobTests)

    add_test(
        NAME batch-output-override
        COMMAND ${CMAKE_COMMAND}
                -DBATCH=$<TARGET_FILE:MyRendererBatch>
                -DJOB=${CMAKE_CURRENT_SOURCE_DIR}/assets/renderjobs/02_cpu_openexr.renderjob
                -DMULTI_JOB=${CMAKE_CURRENT_SOURCE_DIR}/assets/renderjobs/01_cpu_reference.renderjob
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/batch-output-override-test
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/BatchOutputOverrideTest.cmake
    )

    add_executable(MyRendererRasterCaptureTests tests/RasterCaptureTests.cpp)
    target_link_libraries(MyRendererRasterCaptureTests PRIVATE MyRendererBatchRuntime glad_gl_core_33)
    target_compile_definitions(MyRendererRasterCaptureTests PRIVATE MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(MyRendererRasterCaptureTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererRasterCaptureTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME raster-capture-contract COMMAND MyRendererRasterCaptureTests)
    add_custom_target(cloud-determinism-acceptance
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/cloud-determinism-acceptance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/CloudDeterminismAcceptance.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking raster cloud repeatability, warmup invariance and capture metadata")

    add_executable(MyRendererRenderQueueTests tests/RenderQueueTests.cpp) # Capture contract is separate from queue lifecycle.
    target_link_libraries(MyRendererRenderQueueTests PRIVATE MyRendererBatchRuntime MyRendererModules)
    target_compile_definitions(MyRendererRenderQueueTests PRIVATE
        MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(MSVC)
        target_compile_options(MyRendererRenderQueueTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererRenderQueueTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME render-queue-runtime COMMAND MyRendererRenderQueueTests)

    add_executable(MyRendererEditorSessionTests
        tests/EditorSessionTests.cpp
        src/app/EditorSession.cpp
    )
    target_include_directories(MyRendererEditorSessionTests PRIVATE src)
    if(MSVC)
        target_compile_options(MyRendererEditorSessionTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererEditorSessionTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME editor-session COMMAND MyRendererEditorSessionTests)

    add_executable(MyRendererCameraNavigationTests
        tests/CameraNavigationTests.cpp
        src/render/Camera.cpp
    )
    target_include_directories(MyRendererCameraNavigationTests PRIVATE src)
    target_link_libraries(MyRendererCameraNavigationTests PRIVATE glm::glm)
    if(MSVC)
        target_compile_options(MyRendererCameraNavigationTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererCameraNavigationTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME camera-navigation COMMAND MyRendererCameraNavigationTests)

    # Cascade fitting is pure matrix math, so it is verified without an OpenGL context: the split
    # scheme, the sub-frustum corners, the box containment and the texel-grid stability are the
    # parts of a CSM that go wrong silently.
    add_executable(MyRendererShadowCascadeTests
        tests/ShadowCascadeTests.cpp
        src/render/ShadowCascade.cpp
    )
    target_link_libraries(MyRendererShadowCascadeTests PRIVATE glm::glm)
    target_include_directories(MyRendererShadowCascadeTests PRIVATE src)
    if(MSVC)
        target_compile_options(MyRendererShadowCascadeTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererShadowCascadeTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME shadow-cascade-fitting COMMAND MyRendererShadowCascadeTests)

    add_executable(MyRendererWaterWavesTests
        tests/WaterWavesTests.cpp
        src/render/WaterWaves.cpp
    )
    target_link_libraries(MyRendererWaterWavesTests PRIVATE glm::glm)
    target_include_directories(MyRendererWaterWavesTests PRIVATE src)
    if(MSVC)
        target_compile_options(MyRendererWaterWavesTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererWaterWavesTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME water-wave-synthesis COMMAND MyRendererWaterWavesTests)

    add_executable(MyRendererAtmosphereTests tests/AtmosphereTests.cpp)
    target_link_libraries(MyRendererAtmosphereTests PRIVATE MyRendererPathTracing)
    target_include_directories(MyRendererAtmosphereTests PRIVATE src)
    if(MSVC)
        target_compile_options(MyRendererAtmosphereTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererAtmosphereTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME atmosphere-model COMMAND MyRendererAtmosphereTests)

    # The CPU reference raymarcher is the golden value the GPU march is checked against, so its own
    # contracts are tested separately from the analytic sky it shares a density field with.
    add_executable(MyRendererCloudTests tests/CloudReferenceTests.cpp)
    target_link_libraries(MyRendererCloudTests PRIVATE MyRendererPathTracing)
    target_include_directories(MyRendererCloudTests PRIVATE src)
    if(MSVC)
        target_compile_options(MyRendererCloudTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererCloudTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME cloud-reference COMMAND MyRendererCloudTests)
    add_test(NAME cloud-shape-acceptance COMMAND MyRendererCloudCalibration
        --acceptance ${CMAKE_CURRENT_BINARY_DIR}/cloud-calibration)
    set_tests_properties(cloud-shape-acceptance PROPERTIES TIMEOUT 60)

    add_executable(MyRendererTimelineTests tests/TimelineTests.cpp)
    target_include_directories(MyRendererTimelineTests PRIVATE src)
    if(MSVC)
        target_compile_options(MyRendererTimelineTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererTimelineTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME runtime-timeline COMMAND MyRendererTimelineTests)

    add_executable(MyRendererModuleTests
        tests/ModuleRuntimeTests.cpp
        src/scene/Scene.cpp
    )
    target_link_libraries(MyRendererModuleTests PRIVATE MyRendererModules glad_gl_core_33)
    target_include_directories(MyRendererModuleTests PRIVATE src)
    if(MSVC)
        target_compile_options(MyRendererModuleTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererModuleTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME module-runtime COMMAND MyRendererModuleTests)

    add_executable(MyRendererWorkspaceAssetsTests
        tests/WorkspaceAssetsTests.cpp
        src/app/WorkspaceAssets.cpp
    )
    target_include_directories(MyRendererWorkspaceAssetsTests PRIVATE src)
    if(MSVC)
        target_compile_options(MyRendererWorkspaceAssetsTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererWorkspaceAssetsTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME workspace-assets COMMAND MyRendererWorkspaceAssetsTests)

    add_executable(MyRendererAssetThumbnailTests
        tests/AssetThumbnailTests.cpp
        src/app/AssetThumbnail.cpp
        src/app/WorkspaceAssets.cpp
    )
    target_include_directories(MyRendererAssetThumbnailTests PRIVATE src)
    target_compile_definitions(MyRendererAssetThumbnailTests PRIVATE
        MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(MyRendererAssetThumbnailTests PRIVATE
        MyRendererBatchRuntime MyRendererModules glad_gl_core_33)
    if(MSVC)
        target_compile_options(MyRendererAssetThumbnailTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererAssetThumbnailTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME asset-thumbnails COMMAND MyRendererAssetThumbnailTests)
    add_custom_target(asset-thumbnail-layout-acceptance
        COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_ASSET_THUMBNAIL_ACCEPTANCE=1
            MYRENDERER_EDITOR_WINDOW_WIDTH=1100
            MYRENDERER_EDITOR_WINDOW_HEIGHT=680
            MYRENDERER_EDITOR_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/asset-thumbnail-layout-1100x680.png
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/fixtures/01_multi_model_hierarchy.myscene
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking raster thumbnail upload and the 1100x680 editor layout"
    )

    add_executable(MyRendererAssetTests
        tests/AssetImportTests.cpp
        src/io/AssimpImporter.cpp
        src/io/GltfMaterialExtensions.cpp
        src/io/ObjLoader.cpp
    )
    target_include_directories(MyRendererAssetTests PRIVATE
        src
        ${assimp_SOURCE_DIR}/contrib/rapidjson/include
    )
    target_compile_definitions(MyRendererAssetTests PRIVATE
        MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(MyRendererAssetTests PRIVATE
        glm::glm
        assimp::assimp
        tinyobjloader
    )
    if(MSVC)
        target_compile_options(MyRendererAssetTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererAssetTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME asset-import COMMAND MyRendererAssetTests)

    add_executable(MyRendererSceneTests
        tests/SceneDrawListTests.cpp
        src/render/SceneDrawList.cpp
    )
    target_include_directories(MyRendererSceneTests PRIVATE src)
    target_link_libraries(MyRendererSceneTests PRIVATE glm::glm)
    if(MSVC)
        target_compile_options(MyRendererSceneTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererSceneTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME scene-draw-list COMMAND MyRendererSceneTests)

    add_executable(MyRendererSceneGraphTests
        tests/SceneTests.cpp
        src/scene/Scene.cpp
    )
    target_include_directories(MyRendererSceneGraphTests PRIVATE src)
    target_link_libraries(MyRendererSceneGraphTests PRIVATE glm::glm)
    if(MSVC)
        target_compile_options(MyRendererSceneGraphTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererSceneGraphTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME scene-graph COMMAND MyRendererSceneGraphTests)

    add_executable(MyRendererSceneDocumentTests
        tests/SceneDocumentTests.cpp
        src/scene/SceneDocument.cpp
    )
    target_include_directories(MyRendererSceneDocumentTests PRIVATE
        src
        ${assimp_SOURCE_DIR}/contrib/rapidjson/include
    )
    target_compile_definitions(MyRendererSceneDocumentTests PRIVATE
        MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(MyRendererSceneDocumentTests PRIVATE glm::glm glad_gl_core_33 MyRendererCloudNoise)
    if(MSVC)
        target_compile_options(MyRendererSceneDocumentTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererSceneDocumentTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME scene-document-repeat-load COMMAND MyRendererSceneDocumentTests)

    add_executable(MyRendererPathTracingTests tests/PathTracingFoundationTests.cpp)
    target_link_libraries(MyRendererPathTracingTests PRIVATE MyRendererPathTracing)
    if(MSVC)
        target_compile_options(MyRendererPathTracingTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererPathTracingTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME path-tracing-foundation COMMAND MyRendererPathTracingTests)
    add_executable(MyRendererProgressiveTests tests/ProgressiveRenderingTests.cpp)
    target_link_libraries(MyRendererProgressiveTests PRIVATE MyRendererPathTracing)
    target_include_directories(MyRendererProgressiveTests PRIVATE ${assimp_SOURCE_DIR}/contrib/stb)
    add_test(NAME path-tracing-progressive COMMAND MyRendererProgressiveTests)
    set_tests_properties(path-tracing-progressive PROPERTIES TIMEOUT 30)
    add_executable(MyRendererCpuPreviewTests tests/CpuProgressivePreviewTests.cpp)
    target_link_libraries(MyRendererCpuPreviewTests PRIVATE MyRendererPathTracing)
    target_include_directories(MyRendererCpuPreviewTests PRIVATE ${assimp_SOURCE_DIR}/contrib/stb)
    add_test(NAME cpu-progressive-preview COMMAND MyRendererCpuPreviewTests)
    set_tests_properties(cpu-progressive-preview PROPERTIES TIMEOUT 30)
    add_executable(MyRendererDenoisingTests tests/DenoisingTests.cpp)
    target_link_libraries(MyRendererDenoisingTests PRIVATE MyRendererPathTracing)
    add_test(NAME path-tracing-denoising COMMAND MyRendererDenoisingTests)
    set_tests_properties(path-tracing-denoising PROPERTIES TIMEOUT 30)
    add_custom_target(path-tracing-regression
        COMMAND MyRendererProgressiveTests
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/baselines/reference-images/sr-p1g-volume-glass
            ${CMAKE_CURRENT_BINARY_DIR}/path-tracing-acceptance/volume-glass
        DEPENDS path-tracing-acceptance MyRendererProgressiveTests
        COMMENT "Comparing Beauty and seven AOV HDR/PNG outputs against the fixed SAH volume-glass baseline"
    )

    add_executable(MyRendererShaderHotReloadTests
        tests/ShaderHotReloadTests.cpp
        src/render/Shader.cpp
    )
    target_include_directories(MyRendererShaderHotReloadTests PRIVATE src)
    target_link_libraries(MyRendererShaderHotReloadTests PRIVATE glad_gl_core_33 glfw glm::glm)
    if(MSVC)
        target_compile_options(MyRendererShaderHotReloadTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererShaderHotReloadTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_custom_target(shader-hot-reload-smoke
        COMMAND $<TARGET_FILE:MyRendererShaderHotReloadTests>
        DEPENDS MyRendererShaderHotReloadTests
        COMMENT "Verifying fail-safe Shader hot reload with a real OpenGL context"
    )

    # GPU / CPU parity for the shared cloud density field. Needs a real context because the whole
    # point is to run the field through a GPU driver's compiler and compare it with the C++ one, so
    # it is a `gpu-*` custom target rather than a CTest case: a machine without a GPU must still be
    # able to run the CPU suite.
    add_executable(MyRendererCloudCaptureInputTests tests/CloudCaptureInputTests.cpp)
    target_link_libraries(MyRendererCloudCaptureInputTests PRIVATE MyRendererCloudNoise)
    add_test(NAME cloud-capture-inputs COMMAND MyRendererCloudCaptureInputTests)
    add_executable(MyRendererCloudNoiseTests tests/CloudNoiseVolumeTests.cpp)
    target_link_libraries(MyRendererCloudNoiseTests PRIVATE MyRendererCloudNoise)
    add_test(NAME cloud-noise-asset COMMAND MyRendererCloudNoiseTests)
    add_executable(MyRendererCloudNoiseGpuTests
        tests/CloudNoiseGpuTests.cpp src/render/CloudNoiseTexture.cpp
        src/render/Shader.cpp src/render/OpenGlStateCache.cpp)
    target_include_directories(MyRendererCloudNoiseGpuTests PRIVATE src)
    target_compile_definitions(MyRendererCloudNoiseGpuTests PRIVATE
        MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(MyRendererCloudNoiseGpuTests PRIVATE
        MyRendererCloudNoise glad_gl_core_33 glfw glm::glm)
    foreach(target MyRendererCloudNoiseAsset MyRendererCloudLightingAsset MyRendererCloudCaptureInputTests MyRendererCloudNoiseTests MyRendererCloudNoiseGpuTests)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()
    add_custom_target(cloud-noise-acceptance
        COMMAND $<TARGET_FILE:MyRendererCloudNoiseGpuTests> ${CMAKE_CURRENT_BINARY_DIR}/cloud-noise-acceptance
        COMMAND ${CMAKE_COMMAND}
            -DGENERATOR=$<TARGET_FILE:MyRendererCloudNoiseAsset>
            -DCANONICAL_ASSET=${CMAKE_CURRENT_SOURCE_DIR}/assets/clouds/noise-v1-64-period4.cloudnoise
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/cloud-noise-acceptance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/CloudNoiseAssetAcceptance.cmake
        DEPENDS MyRendererCloudNoiseGpuTests MyRendererCloudNoiseAsset
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking offline cloud noise identity, corruption and CPU/GPU sampling")

    add_executable(MyRendererCloudFieldParityTests
        tests/CloudFieldParityTests.cpp
        src/render/Shader.cpp
    )
    target_include_directories(MyRendererCloudFieldParityTests PRIVATE src)
    target_compile_definitions(MyRendererCloudFieldParityTests PRIVATE
        MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(MyRendererCloudFieldParityTests PRIVATE glad_gl_core_33 glfw glm::glm MyRendererCloudNoise)
    if(MSVC)
        target_compile_options(MyRendererCloudFieldParityTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererCloudFieldParityTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_custom_target(cloud-field-parity
        COMMAND $<TARGET_FILE:MyRendererCloudFieldParityTests>
        DEPENDS MyRendererCloudFieldParityTests
        COMMENT "Comparing the shared cloud density field on the GPU against the CPU"
    )

    # The GPU ray march against the CPU reference integral. Separate from the field parity above
    # because the two answer different questions: that one asks whether both compilers evaluate one
    # density field, this one whether they integrate it the same way.
    add_executable(MyRendererCloudMarchParityTests
        tests/CloudMarchParityTests.cpp
        src/render/Shader.cpp
        src/render/CloudLayerRenderer.cpp
        src/render/Camera.cpp
    )
    target_include_directories(MyRendererCloudMarchParityTests PRIVATE src)
    target_compile_definitions(MyRendererCloudMarchParityTests PRIVATE
        MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(MyRendererCloudMarchParityTests PRIVATE
        MyRendererPathTracing glad_gl_core_33 glfw glm::glm
    )
    if(MSVC)
        target_compile_options(MyRendererCloudMarchParityTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererCloudMarchParityTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_custom_target(cloud-march-parity
        COMMAND $<TARGET_FILE:MyRendererCloudMarchParityTests>
        DEPENDS MyRendererCloudMarchParityTests
        COMMENT "Comparing the GPU cloud ray march against the CPU reference integral"
    )

    add_executable(MyRendererCloudShadowTests
        tests/CloudShadowTests.cpp
        src/render/CloudShadowRenderer.cpp
        src/render/Shader.cpp)
    target_include_directories(MyRendererCloudShadowTests PRIVATE src)
    target_compile_definitions(MyRendererCloudShadowTests PRIVATE
        MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(MyRendererCloudShadowTests PRIVATE
        MyRendererPathTracing glad_gl_core_33 glfw glm::glm)
    if(MSVC)
        target_compile_options(MyRendererCloudShadowTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererCloudShadowTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_custom_target(cloud-shadow-acceptance
        COMMAND $<TARGET_FILE:MyRendererCloudShadowTests> ${CMAKE_CURRENT_BINARY_DIR}/cloud-shadow-acceptance
        DEPENDS MyRendererCloudShadowTests
        COMMENT "Checking shared sun-ray transmission, shadow advection and stable projection")

    add_executable(MyRendererGodRaysTests
        tests/GodRaysTests.cpp src/render/GodRaysRenderer.cpp src/render/CloudShadowRenderer.cpp
        src/render/Camera.cpp src/render/Shader.cpp)
    target_include_directories(MyRendererGodRaysTests PRIVATE src)
    target_compile_definitions(MyRendererGodRaysTests PRIVATE MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(MyRendererGodRaysTests PRIVATE MyRendererPathTracing glad_gl_core_33 glfw glm::glm)
    if(MSVC)
        target_compile_options(MyRendererGodRaysTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererGodRaysTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_custom_target(god-rays-acceptance
        COMMAND $<TARGET_FILE:MyRendererGodRaysTests> ${CMAKE_CURRENT_BINARY_DIR}/god-rays-acceptance
        DEPENDS MyRendererGodRaysTests
        COMMENT "Checking radial scattering, cloud/geometry occlusion, solar transmission and skip behavior")

    add_executable(MyRendererCloudTemporalTests
        tests/CloudTemporalTests.cpp
        src/render/Shader.cpp
        src/render/CloudLayerRenderer.cpp
        src/render/Camera.cpp
    )
    target_include_directories(MyRendererCloudTemporalTests PRIVATE src)
    target_compile_definitions(MyRendererCloudTemporalTests PRIVATE
        MYRENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(MyRendererCloudTemporalTests PRIVATE
        MyRendererPathTracing glad_gl_core_33 glfw glm::glm)
    if(MSVC)
        target_compile_options(MyRendererCloudTemporalTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererCloudTemporalTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    foreach(cloudTarget MyRendererCloudMarchParityTests MyRendererCloudShadowTests MyRendererGodRaysTests MyRendererCloudTemporalTests)
        target_sources(${cloudTarget} PRIVATE src/render/CloudNoiseTexture.cpp)
    endforeach()
    add_custom_target(cloud-temporal-acceptance
        COMMAND $<TARGET_FILE:MyRendererCloudTemporalTests>
            ${CMAKE_CURRENT_BINARY_DIR}/cloud-temporal-acceptance
        DEPENDS MyRendererCloudTemporalTests
        COMMENT "Checking cloud history, half-resolution depth, flicker and camera/wind motion")

    add_executable(MyRendererPrismOpticsTests
        tests/PrismOpticsTests.cpp
    src/optics/PrismOptics.cpp
        src/optics/PrismDemo.cpp
        src/optics/SpectralBeamMesh.cpp
    )
    target_include_directories(MyRendererPrismOpticsTests PRIVATE src)
    target_link_libraries(MyRendererPrismOpticsTests PRIVATE glm::glm)
    if(MSVC)
        target_compile_options(MyRendererPrismOpticsTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererPrismOpticsTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME prism-optics COMMAND MyRendererPrismOpticsTests)

    add_executable(MyRendererImageComparison tests/ImageComparison.cpp)
    target_include_directories(MyRendererImageComparison PRIVATE
        ${assimp_SOURCE_DIR}/contrib/zlib
        ${assimp_BINARY_DIR}/contrib/zlib
    )
    target_link_libraries(MyRendererImageComparison PRIVATE zlibstatic)
    add_dependencies(coastal-sequence-acceptance MyRendererImageComparison)
    if(MSVC)
        target_compile_options(MyRendererImageComparison PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MyRendererImageComparison PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_custom_target(shadow-cascade-acceptance
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/shadow-cascade-acceptance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/ShadowCascadeAcceptance.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking CSM splits in Forward/Deferred and recording shadow GPU costs"
    )

    add_custom_target(water-synthesis-acceptance
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/water-synthesis-acceptance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/WaterSynthesisAcceptance.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking ocean transmission, underwater fog, sea states, quality, parity, and motion"
    )

    add_custom_target(cloud-layer-acceptance
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/cloud-layer-acceptance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/CloudLayerAcceptance.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing calibrated cloud presets and checking deterministic volume compositing"
    )

    add_custom_target(cloud-layer-benchmark
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/cloud-layer-benchmark
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/CloudLayerBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Measuring independent cloud GPU cost at 1280x720 for Low and High"
    )
    add_custom_target(god-rays-benchmark
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/god-rays-benchmark
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/GodRaysBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Measuring Low/High half-resolution radial scattering at 1280x720")
    add_custom_target(god-rays-visual-acceptance
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/god-rays-visual-acceptance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/GodRaysVisualAcceptance.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking god rays visibility, render paths, night/behind-camera skip and repeatability")
    add_custom_target(cloud-shadow-benchmark
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/cloud-shadow-benchmark
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/CloudShadowBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Measuring the independent solar cloud transmission pass")

    add_custom_target(cloud-shadow-visual-acceptance
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/cloud-shadow-visual-acceptance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/CloudShadowVisualAcceptance.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking cloud shadows on ground/water, render-path parity and night skip")

    if(WIN32)
        find_program(MYRENDERER_POWERSHELL NAMES pwsh powershell REQUIRED)
        add_custom_target(cloud-capture-mutation-acceptance
            COMMAND ${MYRENDERER_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/tools/CloudCaptureMutationAcceptance.ps1
                -Renderer $<TARGET_FILE:MyRenderer> -SourceRoot ${CMAKE_CURRENT_SOURCE_DIR}
                -OutputDirectory ${CMAKE_CURRENT_BINARY_DIR}/cloud-capture-mutation-acceptance
            DEPENDS MyRenderer VERBATIM)
    endif()
    add_custom_target(cloud-offline-production-acceptance
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/cloud-offline-production-acceptance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/CloudOfflineProductionAcceptance.cmake
        COMMAND ${CMAKE_COMMAND} -E env MYRENDERER_TEST_OFFLINE_NOISE=1
            $<TARGET_FILE:MyRendererCloudMarchParityTests>
        COMMAND ${CMAKE_COMMAND} -E env MYRENDERER_TEST_OFFLINE_NOISE=1
            $<TARGET_FILE:MyRendererCloudShadowTests> ${CMAKE_CURRENT_BINARY_DIR}/cloud-offline-production-acceptance/shadow
        DEPENDS MyRenderer MyRendererImageComparison MyRendererCloudMarchParityTests MyRendererCloudShadowTests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking production offline cloud appearance, integrals and GPU cost")

    add_custom_target(camera-environment-acceptance
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/camera-environment-acceptance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/CameraEnvironmentAcceptance.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking camera motion does not rebake the cloudless environment")

    add_custom_target(cloud-temporal-visual-acceptance
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/cloud-temporal-visual-acceptance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/CloudTemporalVisualAcceptance.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking final cloud composites and reproducible independent history"
    )
    add_custom_target(cloud-temporal-benchmark
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/cloud-temporal-benchmark
            -DHALF_RESOLUTION=1 -DTEMPORAL=1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/CloudLayerBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Measuring half-resolution cloud march and independent history at 1280x720"
    )

    add_custom_target(water-synthesis-benchmark
        COMMAND ${CMAKE_COMMAND}
            -DRENDERER=$<TARGET_FILE:MyRenderer>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/water-synthesis-benchmark
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/WaterSynthesisBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Measuring ocean Low/High GPU budgets against the same dry scene"
    )

    add_custom_target(module-rendering-acceptance
        COMMAND ${CMAKE_COMMAND}
                -DBATCH=$<TARGET_FILE:MyRendererBatch>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/module-rendering-acceptance
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/ModuleRenderingAcceptance.cmake
        DEPENDS MyRendererBatch MyRendererImageComparison MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Accepting module simulate/bake, cache reuse, stale rejection, and reproducible sequences"
    )

    add_custom_target(stylized-acceptance
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/stylized-acceptance
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/StylizedAcceptance.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing SR-P2A-C effects, debug views, presets, render paths, resolutions, TAA, and transparent boundaries"
    )

    add_custom_target(stylized-benchmark
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/stylized-benchmarks
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/StylizedBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Measuring SR-P2C Low and High stylized GPU tiers"
    )

    add_custom_target(prism5-visual-regression
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/prism5-visual-current
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/Prism5VisualRegression.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing and comparing the Prism-5 visual regression matrix"
    )

    add_custom_target(prism5-reel-frames
        COMMAND ${CMAKE_COMMAND} -E env
                MYRENDERER_PRISM_REEL_DIR=${CMAKE_CURRENT_BINARY_DIR}/prism5-reel-frames
                MYRENDERER_PRISM_REEL_FRAMES=360
                MYRENDERER_RENDER_WIDTH=1280
                MYRENDERER_RENDER_HEIGHT=720
                $<TARGET_FILE:MyRenderer>
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing 15 seconds of deterministic Prism-5 reel frames at 24 fps"
    )

    add_custom_target(prism5-benchmark
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/prism5-benchmarks
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/Prism5Benchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Benchmarking Prism-5 at 1920x1080, 4x MSAA, and four spectral tiers"
    )

    add_custom_target(glass2c-visual-regression
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/glass2c-visual-current
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/Glass2cVisualRegression.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing and comparing the Glass-2C visual regression matrix"
    )

    add_custom_target(glass2c-benchmark
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/glass2c-benchmarks
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/Glass2cBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Benchmarking Glass-2C at 1920x1080 with 1x/4x MSAA"
    )

    add_custom_target(glass3-visual-regression
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/glass3-visual-current
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/Glass3VisualRegression.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing and comparing the Glass-3 caustics regression matrix"
    )

    add_custom_target(glass3-benchmark
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/glass3-benchmarks
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/Glass3Benchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Benchmarking Glass-3 Off, Projector, and Light-space modes"
    )

    add_custom_target(glass4-visual-regression
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/glass4-visual-current
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/Glass4VisualRegression.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing and comparing the Glass-4 portfolio regression matrix"
    )

    add_custom_target(glass4-benchmark
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/glass4-benchmarks
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/Glass4Benchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Benchmarking Glass-4 per-pass GPU time at 1920x1080 and 4x MSAA"
    )

    add_custom_target(deferred-visual-regression
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/deferred-visual-current
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/DeferredVisualRegression.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing and comparing the GP-P1 Forward/Deferred regression matrix"
    )

    add_custom_target(deferred-benchmark
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/deferred-benchmarks
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/DeferredBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Benchmarking GP-P1 Forward/Deferred at 1080p with 1x/4x MSAA"
    )

    add_custom_target(local-lights-visual-regression
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/local-lights-visual-current
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/LocalLightsVisualRegression.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing and comparing the GP-P1B local-light stress matrix"
    )

    add_custom_target(local-lights-benchmark
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/local-lights-benchmarks
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/LocalLightsBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Benchmarking Forward/Deferred with 8, 32, and 64 local lights"
    )

    add_custom_target(instance-stress-visual-regression
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/instance-stress-visual-current
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/InstanceStressVisualRegression.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing and comparing the GP-P1C instance/culling/LOD scene"
    )

    add_custom_target(instance-stress-benchmark
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/instance-stress-benchmarks
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/InstanceStressBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Benchmarking GP-P1C baseline, instancing, culling, and LOD stages"
    )

    add_custom_target(screen-space-visual-regression
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/screen-space-visual-current
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/ScreenSpaceVisualRegression.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing and comparing GP-P1D SSAO/TAA static and motion views"
    )

    add_custom_target(screen-space-benchmark
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/screen-space-benchmarks
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/ScreenSpaceBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Benchmarking GP-P1D SSAO and static/moving TAA"
    )

    add_custom_target(skinning-visual-regression
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/skinning-visual-current
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/SkinningVisualRegression.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing and comparing GP-P1E bind, animated, joint, and weight views"
    )

    add_custom_target(skinning-benchmark
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/skinning-benchmarks
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/SkinningBenchmark.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Benchmarking GP-P1E bind pose, GPU skinning, and debug output"
    )

    add_custom_target(foundation-visual-regression
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/foundation-visual-current
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/FoundationVisualRegression.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Capturing SR-P0 scene entities and dynamic motion vectors"
    )

    add_custom_target(renderer-regression-suite
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DCOMPARATOR=$<TARGET_FILE:MyRendererImageComparison>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/RendererRegressionSuite.cmake
        DEPENDS MyRenderer MyRendererImageComparison
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running the complete deterministic renderer visual regression suite"
    )

    add_custom_target(renderer-benchmark-suite
        COMMAND ${CMAKE_COMMAND}
                -DRENDERER=$<TARGET_FILE:MyRenderer>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/RendererBenchmarkSuite.cmake
        DEPENDS MyRenderer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running the complete renderer benchmark suite"
    )

    find_program(MYRENDERER_NSYS_EXECUTABLE
        NAMES nsys nsys.exe
        HINTS
            "C:/Program Files/NVIDIA Corporation/Nsight Systems 2024.5.1/target-windows-x64"
    )
    if(MYRENDERER_NSYS_EXECUTABLE)
        add_custom_target(glass4-nsight-capture
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    ${CMAKE_CURRENT_BINARY_DIR}/glass4-captures
            COMMAND ${CMAKE_COMMAND} -E env
                    MYRENDERER_GLASS3_DEMO=1
                    MYRENDERER_SMOKE_TEST=1
                    MYRENDERER_MSAA=4
                    MYRENDERER_RENDER_WIDTH=1920
                    MYRENDERER_RENDER_HEIGHT=1080
                    MYRENDERER_CAUSTICS=1
                    MYRENDERER_CAUSTICS_MODE=1
                    ${MYRENDERER_NSYS_EXECUTABLE} profile
                    --trace=opengl,opengl-annotations
                    --sample=none
                    --force-overwrite=true
                    --output=${CMAKE_CURRENT_BINARY_DIR}/glass4-captures/glass4_caustics
                    $<TARGET_FILE:MyRenderer>
            DEPENDS MyRenderer
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Saving a Glass-4 Nsight Systems capture with KHR_debug pass ranges"
        )
    endif()
endif()

add_custom_target(gpu-smoke
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_CPU_PREVIEW=1
            MYRENDERER_CPU_PREVIEW_SCALE=1
            MYRENDERER_CPU_PREVIEW_SPP=1
            MYRENDERER_RENDER_WIDTH=160
            MYRENDERER_RENDER_HEIGHT=120
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/cube.obj
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_ANIMATION_DEMO=1
            MYRENDERER_ANIMATION=1
            MYRENDERER_ANIMATION_TIME=1.0
            MYRENDERER_RENDER_PATH=1
            $<TARGET_FILE:MyRenderer>
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/material_regression.obj
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_SCENE_DEMO=1
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/alpha_material_test.gltf
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/glass_material_test.gltf
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_GBUFFER_DEBUG=0
            MYRENDERER_SSAO=1
            MYRENDERER_TAA=1
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/pbr_material_test.gltf
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_GBUFFER_DEBUG=0
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/pbr_material_test.gltf
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_GBUFFER_DEBUG=0
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/glass_material_test.gltf
    # The analytic sky lights the scene itself now, so its per-channel key light has to survive a
    # real context in both shading paths rather than only in the model tests. The fixture enables
    # aerial perspective, so these also compile the compositor's segment integration.
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_SUN_ELEVATION=8
            MYRENDERER_SUN_AZIMUTH=120
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/fixtures/18_atmosphere_sky.myscene
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_GBUFFER_DEBUG=0
            MYRENDERER_SUN_ELEVATION=8
            MYRENDERER_SUN_AZIMUTH=120
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/fixtures/18_atmosphere_sky.myscene
    # The On/Off and scale-height pairs are what the slice is accepted against, and each is a
    # distinct uniform path through the compositor.
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_AERIAL_PERSPECTIVE=0
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/fixtures/18_atmosphere_sky.myscene
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_AERIAL_PERSPECTIVE=1
            MYRENDERER_AERIAL_STRENGTH=3
            MYRENDERER_AERIAL_SCALE_HEIGHT=2
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/fixtures/18_atmosphere_sky.myscene
    # Cascaded shadows: one, three and four cascades are three different uniform paths through the
    # cascade array, and the open coastal fixture is where the split distances actually matter.
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_SHADOW_CASCADES=1
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/fixtures/19_coastal_cascades.myscene
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_SHADOW_CASCADES=3
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/fixtures/19_coastal_cascades.myscene
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_GBUFFER_DEBUG=0
            MYRENDERER_SHADOW_CASCADES=4
            MYRENDERER_SHADOW_SPLIT_LAMBDA=0.5
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/fixtures/19_coastal_cascades.myscene
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_LIGHT_STRESS=1
            MYRENDERER_LOCAL_LIGHT_TIER=2
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/cube.obj
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=0
            MYRENDERER_INSTANCE_STRESS=1
            MYRENDERER_INSTANCE_OPTIMIZATION=1
            MYRENDERER_FRUSTUM_CULLING=1
            MYRENDERER_LOD=1
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/sphere.obj
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=4
            MYRENDERER_PRISM_DEMO=1
            MYRENDERER_PRISM_SAMPLES=21
            $<TARGET_FILE:MyRenderer>
    COMMAND ${CMAKE_COMMAND} -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_MSAA=1
            MYRENDERER_RECOVERY_TEST=${CMAKE_CURRENT_SOURCE_DIR}/assets/models/does_not_exist.obj
            $<TARGET_FILE:MyRenderer>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/cube.obj
    DEPENDS MyRenderer
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running Iris with a real OpenGL context"
)

# Keep internal target names stable for existing build scripts.
set_target_properties(MyRenderer PROPERTIES OUTPUT_NAME Iris)
set_target_properties(MyRendererBatch PROPERTIES OUTPUT_NAME IrisBatch)

install(TARGETS MyRenderer MyRendererBatch RUNTIME DESTINATION . COMPONENT Runtime)
install(DIRECTORY shaders/ DESTINATION shaders COMPONENT Runtime)
install(
    DIRECTORY assets/
    DESTINATION assets
    COMPONENT Runtime
    PATTERN "models/bunny*" EXCLUDE
    PATTERN "models/cow.dae" EXCLUDE
    PATTERN "models/dragon2.dae" EXCLUDE
    PATTERN "models/spot*" EXCLUDE
    PATTERN "models/textures/spot*" EXCLUDE
    PATTERN "models/textures/hmap.jpg" EXCLUDE
)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md ASSET_LICENSES.md DESTINATION . COMPONENT Runtime)
install(FILES ${glfw_SOURCE_DIR}/LICENSE.md DESTINATION licenses RENAME GLFW-LICENSE.md COMPONENT Runtime)
install(FILES ${glm_SOURCE_DIR}/copying.txt DESTINATION licenses RENAME GLM-LICENSE.txt COMPONENT Runtime)
install(FILES ${MYRENDERER_GLAD_FETCH_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME GLAD-LICENSE.txt COMPONENT Runtime)
install(FILES ${tinyobjloader_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME TINYOBJLOADER-LICENSE.txt COMPONENT Runtime)
install(FILES ${tinyexr_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME TINYEXR-LICENSE.txt COMPONENT Runtime)
install(FILES ${imgui_SOURCE_DIR}/LICENSE.txt DESTINATION licenses RENAME IMGUI-LICENSE.txt COMPONENT Runtime)
install(FILES ${assimp_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME ASSIMP-LICENSE.txt COMPONENT Runtime)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "Iris")
set(CPACK_PACKAGE_VENDOR "Iris contributors")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(MYRENDERER_PACKAGE_ARCH "x64")
else()
    set(MYRENDERER_PACKAGE_ARCH "x86")
endif()
set(CPACK_PACKAGE_FILE_NAME "Iris-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${MYRENDERER_PACKAGE_ARCH}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_COMPONENTS_ALL Runtime)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
include(CPack)
