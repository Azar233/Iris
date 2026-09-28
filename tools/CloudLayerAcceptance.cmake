cmake_minimum_required(VERSION 3.20)
foreach(required RENDERER COMPARATOR SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
function(capture name preset enabled tier)
    set(image "${OUTPUT_DIR}/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_CLOUD_OFFLINE_NOISE=0
            MYRENDERER_RENDER_WIDTH=960
            MYRENDERER_RENDER_HEIGHT=540
            MYRENDERER_SCREENSHOT=${image}
            MYRENDERER_CLOUD_PRESET=${preset}
            MYRENDERER_CLOUDS=${enabled}
            MYRENDERER_CLOUD_SHADOWS=0
            MYRENDERER_CLOUD_GOD_RAYS=0
            MYRENDERER_CLOUD_TIER=${tier}
            MYRENDERER_CLOUD_HALF_RESOLUTION=0
            MYRENDERER_CLOUD_TEMPORAL=0
            MYRENDERER_TAA=0
            MYRENDERER_BLOOM=0
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            "${RENDERER}" "${SOURCE_DIR}/assets/scenes/01_volumetric_cloud_lab.myscene"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
        TIMEOUT 90
    )
    if(NOT result EQUAL 0 OR NOT EXISTS "${image}")
        message(FATAL_ERROR "Could not capture ${name}")
    endif()
endfunction()
capture(off cumulus 0 high)
capture(cumulus_low cumulus 1 low)
capture(cumulus_high cumulus 1 high)
capture(cumulus_repeat cumulus 1 high)
capture(stratus_high stratus 1 high)
capture(cirrus_high cirrus 1 high)

execute_process(COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/cumulus_high.png"
    "${OUTPUT_DIR}/cumulus_repeat.png" 0 0 RESULT_VARIABLE repeat_result)
if(NOT repeat_result EQUAL 0)
    message(FATAL_ERROR "Fixed-camera cloud captures must repeat exactly on this GPU")
endif()
execute_process(COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/off.png"
    "${OUTPUT_DIR}/cumulus_high.png" 0.001 0.01 RESULT_VARIABLE off_result)
if(NOT off_result EQUAL 1)
    message(FATAL_ERROR "Cloud On/Off should change the captured image")
endif()
message(STATUS "Calibrated cloud captures: PASS (deterministic repeat and On/Off)")
