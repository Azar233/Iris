cmake_minimum_required(VERSION 3.20)
foreach(required RENDERER COMPARATOR SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
function(capture name scene path enabled elevation)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_CLOUD_OFFLINE_NOISE=0
            MYRENDERER_RENDER_WIDTH=960
            MYRENDERER_RENDER_HEIGHT=540
            MYRENDERER_SCREENSHOT=${OUTPUT_DIR}/${name}.png
            MYRENDERER_RENDER_PATH=${path}
            MYRENDERER_CLOUD_SHADOWS=${enabled}
            MYRENDERER_CLOUD_GOD_RAYS=0
            MYRENDERER_SUN_ELEVATION=${elevation}
            MYRENDERER_SUN_AZIMUTH=135
            MYRENDERER_CLOUD_COVERAGE=0.7
            MYRENDERER_CLOUD_COVERAGE_VARIATION=0
            MYRENDERER_CLOUD_FEATURE_SCALE=1200
            MYRENDERER_CLOUD_WIND_X=0
            MYRENDERER_CLOUD_WIND_Z=0
            MYRENDERER_CLOUD_TEMPORAL=0
            MYRENDERER_ANIMATION_TIME=1.25
            MYRENDERER_ANIMATION_FRAME_STEP=0
            MYRENDERER_TAA=0
            MYRENDERER_BLOOM=0
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            "${RENDERER}" "${SOURCE_DIR}/assets/scenes/${scene}.myscene"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result TIMEOUT 90)
    if(NOT result EQUAL 0 OR NOT EXISTS "${OUTPUT_DIR}/${name}.png")
        message(FATAL_ERROR "Could not capture cloud shadow ${name}")
    endif()
endfunction()
function(compare first second mae fraction expected)
    execute_process(COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/${first}.png"
        "${OUTPUT_DIR}/${second}.png" ${mae} ${fraction} RESULT_VARIABLE result)
    if(NOT result EQUAL expected)
        message(FATAL_ERROR "Cloud shadow comparison ${first}/${second} returned ${result}, expected ${expected}")
    endif()
endfunction()
capture(ground_off fixtures/27_cloud_lab_regression 0 0 45)
capture(ground_on fixtures/27_cloud_lab_regression 0 1 45)
capture(ground_deferred fixtures/27_cloud_lab_regression 1 1 45)
capture(water_off fixtures/23_ocean_clouds 0 0 45)
capture(water_on fixtures/23_ocean_clouds 0 1 45)
capture(water_deferred fixtures/23_ocean_clouds 1 1 45)
capture(water_repeat fixtures/23_ocean_clouds 1 1 45)
capture(night_off fixtures/23_ocean_clouds 1 0 -15)
capture(night_on fixtures/23_ocean_clouds 1 1 -15)
compare(ground_off ground_on 0.0001 0.001 1)
compare(water_off water_on 0.0001 0.001 1)
compare(ground_on ground_deferred 0.008 0.08 0)
compare(water_on water_deferred 0.008 0.08 0)
compare(water_deferred water_repeat 0 0 0)
compare(night_off night_on 0 0 0)
message(STATUS "Cloud shadow visual acceptance: PASS")
