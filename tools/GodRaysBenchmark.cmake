cmake_minimum_required(VERSION 3.20)
foreach(required RENDERER SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(tier low high)
    foreach(enabled 0 1)
        set(report "${OUTPUT_DIR}/${tier}-${enabled}.json")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_BENCHMARK_FRAMES=60 MYRENDERER_BENCHMARK_WARMUP=16
            MYRENDERER_BENCHMARK_OUTPUT=${report}
            MYRENDERER_CLOUD_OFFLINE_NOISE=0
            MYRENDERER_RENDER_WIDTH=1280 MYRENDERER_RENDER_HEIGHT=720
            MYRENDERER_CLOUD_TIER=${tier} MYRENDERER_CLOUD_SHADOWS=1
            MYRENDERER_CLOUD_GOD_RAYS=${enabled} MYRENDERER_CLOUD_RAY_STRENGTH=0.35
            MYRENDERER_SUN_ELEVATION=8 MYRENDERER_SUN_AZIMUTH=172
            MYRENDERER_CLOUD_HALF_RESOLUTION=1 MYRENDERER_CLOUD_TEMPORAL=1
            MYRENDERER_TAA=0 MYRENDERER_BLOOM=0 MYRENDERER_HIDE_SELECTION_OUTLINE=1
            "${RENDERER}" "${SOURCE_DIR}/assets/scenes/02_ocean_weather_hero.myscene"
            WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result TIMEOUT 180)
        if(NOT result EQUAL 0 OR NOT EXISTS "${report}")
            message(FATAL_ERROR "Could not benchmark god rays ${tier}-${enabled}")
        endif()
        file(READ "${report}" json)
        string(JSON p50 ERROR_VARIABLE parse_error GET "${json}" gpuPasses "Cloud god rays" p50Ms)
        if(enabled EQUAL 0)
            if(NOT parse_error)
                message(FATAL_ERROR "Disabled god rays must skip the pass")
            endif()
        else()
            if(parse_error OR p50 LESS_EQUAL 0)
                message(FATAL_ERROR "Missing god rays timing")
            endif()
            string(JSON p95 GET "${json}" gpuPasses "Cloud god rays" p95Ms)
            string(JSON samples GET "${json}" gpuPasses "Cloud god rays" measurements)
            if(samples LESS 30)
                message(FATAL_ERROR "Insufficient GPU timing samples")
            endif()
            message(STATUS "God rays ${tier} GPU P50/P95: ${p50}/${p95} ms")
        endif()
    endforeach()
endforeach()
