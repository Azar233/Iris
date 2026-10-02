cmake_minimum_required(VERSION 3.20)
foreach(required RENDERER SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(scene 01_volumetric_cloud_lab 23_ocean_clouds)
    if(scene STREQUAL "23_ocean_clouds")
        set(scene_path "${SOURCE_DIR}/assets/scenes/fixtures/${scene}.myscene")
    else()
        set(scene_path "${SOURCE_DIR}/assets/scenes/${scene}.myscene")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_BENCHMARK_FRAMES=40
            MYRENDERER_BENCHMARK_WARMUP=8
            MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${scene}.json
            MYRENDERER_RENDER_WIDTH=1280
            MYRENDERER_RENDER_HEIGHT=720
            MYRENDERER_CAMERA_HEIGHT_DEMO_STEP=2
            MYRENDERER_TAA_MOTION_DEMO=1
            "${RENDERER}" "${scene_path}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE output ERROR_VARIABLE errors
        RESULT_VARIABLE result TIMEOUT 120
    )
    file(WRITE "${OUTPUT_DIR}/${scene}.log" "${output}\n${errors}")
    if(NOT result EQUAL 0 OR NOT EXISTS "${OUTPUT_DIR}/${scene}.json")
        message(FATAL_ERROR "Camera motion capture failed: ${scene}")
    endif()
    string(REGEX MATCHALL "Atmosphere environment rebuilt in" rebuilds "${output}")
    list(LENGTH rebuilds count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "${scene}: expected initial sky bake only, found ${count}")
    endif()
    file(READ "${OUTPUT_DIR}/${scene}.json" report)
    string(JSON p50 GET "${report}" cpuFrameP50Ms)
    string(JSON p95 GET "${report}" cpuFrameP95Ms)
    # Detect synchronous bake stalls, allowing broad hardware variation in march cost.
    if(p95 GREATER 200)
        message(FATAL_ERROR "${scene}: camera motion stalls, CPU P95 ${p95} ms")
    endif()
    message(STATUS "${scene}: one sky bake; moving CPU P50/P95 ${p50}/${p95} ms")
endforeach()
