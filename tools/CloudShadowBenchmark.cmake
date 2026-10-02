cmake_minimum_required(VERSION 3.20)
foreach(required RENDERER SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(tier off low high)
    if(tier STREQUAL off)
        set(enabled 0)
        set(quality low)
    else()
        set(enabled 1)
        set(quality ${tier})
    endif()
    set(report "${OUTPUT_DIR}/${tier}.json")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        MYRENDERER_BENCHMARK_FRAMES=60
        MYRENDERER_BENCHMARK_WARMUP=16
        MYRENDERER_BENCHMARK_OUTPUT=${report}
        MYRENDERER_CLOUD_OFFLINE_NOISE=0
        MYRENDERER_RENDER_WIDTH=1280 MYRENDERER_RENDER_HEIGHT=720
        MYRENDERER_CLOUDS=1 MYRENDERER_CLOUD_TIER=${quality}
        MYRENDERER_CLOUD_SHADOWS=${enabled}
        MYRENDERER_CLOUD_GOD_RAYS=0
        MYRENDERER_CLOUD_HALF_RESOLUTION=1 MYRENDERER_CLOUD_TEMPORAL=1
        MYRENDERER_SUN_ELEVATION=45 MYRENDERER_SUN_AZIMUTH=135
        MYRENDERER_TAA=0 MYRENDERER_BLOOM=0 MYRENDERER_HIDE_SELECTION_OUTLINE=1
        "${RENDERER}" "${SOURCE_DIR}/assets/scenes/fixtures/23_ocean_clouds.myscene"
        WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result TIMEOUT 180)
    if(NOT result EQUAL 0 OR NOT EXISTS "${report}")
        message(FATAL_ERROR "Could not benchmark cloud shadows ${tier}")
    endif()
    file(READ "${report}" json)
    string(JSON pass_p50 ERROR_VARIABLE parse_error GET "${json}"
        gpuPasses "Cloud sun transmission" p50Ms)
    if(tier STREQUAL off)
        if(NOT parse_error)
            message(FATAL_ERROR "Disabled cloud shadows must skip the transmission pass")
        endif()
    else()
        if(parse_error OR pass_p50 LESS_EQUAL 0)
            message(FATAL_ERROR "Missing cloud shadow timing for ${tier}")
        endif()
        string(JSON pass_p95 GET "${json}" gpuPasses "Cloud sun transmission" p95Ms)
        string(JSON samples GET "${json}" gpuPasses "Cloud sun transmission" measurements)
        if(samples LESS 30)
            message(FATAL_ERROR "Insufficient timing samples")
        endif()
        message(STATUS "Cloud shadow ${tier} GPU P50/P95: ${pass_p50}/${pass_p95} ms")
    endif()
endforeach()
