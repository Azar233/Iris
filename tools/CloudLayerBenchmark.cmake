cmake_minimum_required(VERSION 3.20)

foreach(required RENDERER SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
if(NOT DEFINED HALF_RESOLUTION)
    set(HALF_RESOLUTION 0)
endif()
if(NOT DEFINED TEMPORAL)
    set(TEMPORAL 0)
endif()

# Use the same scene, weather and camera for each tier. Report costs without inventing a portable
# hardware budget: these are the full-resolution measurements that the C4 optimization must beat.
foreach(tier off low high)
    if(tier STREQUAL off)
        set(enabled 0)
        set(quality low)
    else()
        set(enabled 1)
        set(quality ${tier})
    endif()
    set(report "${OUTPUT_DIR}/${tier}.json")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_BENCHMARK_FRAMES=60
            MYRENDERER_BENCHMARK_WARMUP=16
            MYRENDERER_BENCHMARK_OUTPUT=${report}
            MYRENDERER_CLOUD_OFFLINE_NOISE=0
            MYRENDERER_RENDER_WIDTH=1280
            MYRENDERER_RENDER_HEIGHT=720
            MYRENDERER_CLOUDS=${enabled}
            MYRENDERER_CLOUD_SHADOWS=0
            MYRENDERER_CLOUD_GOD_RAYS=0
            MYRENDERER_CLOUD_TIER=${quality}
            MYRENDERER_CLOUD_HALF_RESOLUTION=${HALF_RESOLUTION}
            MYRENDERER_CLOUD_TEMPORAL=${TEMPORAL}
            MYRENDERER_TAA=0
            MYRENDERER_BLOOM=0
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            "${RENDERER}" "${SOURCE_DIR}/assets/scenes/fixtures/27_cloud_lab_regression.myscene"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
        TIMEOUT 180
    )
    if(NOT result EQUAL 0 OR NOT EXISTS "${report}")
        message(FATAL_ERROR "Could not benchmark clouds ${tier}")
    endif()
    file(READ "${report}" json)
    string(JSON frame_p50 GET "${json}" gpuFrameP50Ms)
    string(JSON frame_p95 GET "${json}" gpuFrameP95Ms)
    string(JSON samples GET "${json}" gpuFrameMeasurements)
    if(samples LESS 30 OR frame_p50 LESS_EQUAL 0)
        message(FATAL_ERROR "Missing GPU frame samples for ${tier}")
    endif()
    string(JSON pass_p50 ERROR_VARIABLE parse_error GET "${json}"
        gpuPasses "Cloud volume march" p50Ms)
    if(tier STREQUAL off)
        if(NOT parse_error)
            message(FATAL_ERROR "Clouds off must skip the volume pass")
        endif()
    else()
        if(parse_error OR pass_p50 LESS_EQUAL 0)
            message(FATAL_ERROR "Missing cloud GPU timing for ${tier}")
        endif()
        string(JSON pass_p95 GET "${json}" gpuPasses "Cloud volume march" p95Ms)
        string(JSON pass_samples GET "${json}" gpuPasses "Cloud volume march" measurements)
        if(pass_samples LESS 30)
            message(FATAL_ERROR "Insufficient cloud GPU samples for ${tier}")
        endif()
        message(STATUS "Cloud ${tier} pass GPU P50/P95: ${pass_p50}/${pass_p95} ms")
    endif()
    message(STATUS "Cloud ${tier} frame GPU P50/P95: ${frame_p50}/${frame_p95} ms")
endforeach()
