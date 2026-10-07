cmake_minimum_required(VERSION 3.20)
foreach(required RENDERER COMPARATOR SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
function(capture name scene tier half temporal)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_BENCHMARK_FRAMES=30
            MYRENDERER_BENCHMARK_WARMUP=8
            MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${name}.json
            MYRENDERER_SCREENSHOT_WARMUP=8
            MYRENDERER_CLOUD_OFFLINE_NOISE=0
            MYRENDERER_RENDER_WIDTH=960
            MYRENDERER_RENDER_HEIGHT=540
            MYRENDERER_SCREENSHOT=${OUTPUT_DIR}/${name}.png
            MYRENDERER_CLOUD_TIER=${tier}
            MYRENDERER_CLOUD_SHADOWS=0
            MYRENDERER_CLOUD_GOD_RAYS=0
            MYRENDERER_CLOUD_HALF_RESOLUTION=${half}
            MYRENDERER_CLOUD_TEMPORAL=${temporal}
            MYRENDERER_ANIMATION_TIME=1.25
            MYRENDERER_ANIMATION_FRAME_STEP=0
            MYRENDERER_TAA=0
            MYRENDERER_BLOOM=0
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            "${RENDERER}" "${SOURCE_DIR}/assets/scenes/${scene}.myscene"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result TIMEOUT 120
    )
    if(NOT result EQUAL 0 OR NOT EXISTS "${OUTPUT_DIR}/${name}.png")
        message(FATAL_ERROR "Cloud capture failed: ${name}")
    endif()
endfunction()
capture(full fixtures/27_cloud_lab_regression high 0 0)
capture(half_raw fixtures/27_cloud_lab_regression high 1 0)
capture(low fixtures/27_cloud_lab_regression low 1 1)
capture(high fixtures/27_cloud_lab_regression high 1 1)
capture(repeat fixtures/27_cloud_lab_regression high 1 1)
capture(hero fixtures/23_ocean_clouds high 1 1)
execute_process(COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/high.png"
    "${OUTPUT_DIR}/repeat.png" 0 0 RESULT_VARIABLE repeat_result)
if(NOT repeat_result EQUAL 0)
    message(FATAL_ERROR "Fixed warmup cloud history must be repeatable on this GPU")
endif()
execute_process(COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/full.png"
    "${OUTPUT_DIR}/high.png" 0.02 0.08 RESULT_VARIABLE quality_result)
if(NOT quality_result EQUAL 0)
    message(FATAL_ERROR "Half-resolution/history cloud composite drift exceeds the full-resolution bound")
endif()
message(STATUS "Cloud temporal visual acceptance: PASS")
