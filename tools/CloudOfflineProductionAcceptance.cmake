cmake_minimum_required(VERSION 3.20)
foreach(required RENDERER COMPARATOR SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(scene 01_volumetric_cloud_lab 23_ocean_clouds)
    if(scene STREQUAL "23_ocean_clouds")
        set(scene_path "${SOURCE_DIR}/assets/scenes/fixtures/${scene}.myscene")
    else()
        set(scene_path "${SOURCE_DIR}/assets/scenes/${scene}.myscene")
    endif()
    foreach(tier low high)
        foreach(offline 0 1)
            set(name "${scene}-${tier}-${offline}")
            execute_process(COMMAND "${CMAKE_COMMAND}" -E env
                MYRENDERER_BENCHMARK_FRAMES=60
                MYRENDERER_BENCHMARK_WARMUP=16
                MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${name}.json
                MYRENDERER_SCREENSHOT=${OUTPUT_DIR}/${name}.png
                MYRENDERER_SCREENSHOT_WARMUP=16
                MYRENDERER_RENDER_WIDTH=1280 MYRENDERER_RENDER_HEIGHT=720
                MYRENDERER_CLOUD_OFFLINE_NOISE=${offline}
                MYRENDERER_CLOUD_TIER=${tier}
                MYRENDERER_DETERMINISM=1
                MYRENDERER_CLOUD_SHADOWS=1 MYRENDERER_CLOUD_GOD_RAYS=1
                MYRENDERER_CLOUD_HALF_RESOLUTION=1
                MYRENDERER_TAA=0 MYRENDERER_BLOOM=0
                MYRENDERER_ANIMATION_TIME=1.25 MYRENDERER_ANIMATION_FRAME_STEP=0
                MYRENDERER_HIDE_SELECTION_OUTLINE=1
                "${RENDERER}" "${scene_path}"
                WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result
                OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
            file(WRITE "${OUTPUT_DIR}/${name}.log" "${output}\n${errors}")
            if(NOT result EQUAL 0 OR NOT EXISTS "${OUTPUT_DIR}/${name}.json"
                OR NOT EXISTS "${OUTPUT_DIR}/${name}.png")
                message(FATAL_ERROR "Offline cloud production capture failed: ${name}")
            endif()
            file(READ "${OUTPUT_DIR}/${name}.json" report)
            string(JSON samples GET "${report}" gpuFrameMeasurements)
            if(samples LESS 30)
                message(FATAL_ERROR "Not enough GPU measurements: ${name}")
            endif()
            string(JSON gpu GET "${report}" gpuFrameP50Ms)
            string(JSON march GET "${report}" gpuPasses "Cloud volume march" p50Ms)
            string(JSON shadow GET "${report}" gpuPasses "Cloud sun transmission" p50Ms)
            message(STATUS "${name}: GPU frame/march/shadow P50 ${gpu}/${march}/${shadow} ms")
        endforeach()
        execute_process(COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/${scene}-${tier}-0.png"
            "${OUTPUT_DIR}/${scene}-${tier}-1.png" 0.02 0.10 RESULT_VARIABLE result
            OUTPUT_VARIABLE comparison ERROR_VARIABLE errors)
        message(STATUS "${comparison}")
        file(WRITE "${OUTPUT_DIR}/${scene}-${tier}-comparison.txt" "${comparison}${errors}")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Offline approximation visibly diverged: ${scene}/${tier}")
        endif()
    endforeach()
endforeach()
