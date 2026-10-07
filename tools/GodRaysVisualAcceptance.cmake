cmake_minimum_required(VERSION 3.20)
foreach(required RENDERER COMPARATOR SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
function(capture name scene enabled path tier elevation azimuth)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        MYRENDERER_CLOUD_OFFLINE_NOISE=0
        MYRENDERER_SMOKE_TEST=1 MYRENDERER_RENDER_WIDTH=960 MYRENDERER_RENDER_HEIGHT=540
        MYRENDERER_SCREENSHOT=${OUTPUT_DIR}/${name}.png
        MYRENDERER_CLOUD_GOD_RAYS=${enabled} MYRENDERER_CLOUD_RAY_STRENGTH=0.35
        MYRENDERER_CLOUD_SHADOWS=1 MYRENDERER_CLOUD_TIER=${tier}
        MYRENDERER_CLOUD_TEMPORAL=0
        MYRENDERER_RENDER_PATH=${path}
        MYRENDERER_SUN_ELEVATION=${elevation} MYRENDERER_SUN_AZIMUTH=${azimuth}
        MYRENDERER_ANIMATION_TIME=1.25 MYRENDERER_ANIMATION_FRAME_STEP=0
        MYRENDERER_TAA=0 MYRENDERER_BLOOM=0 MYRENDERER_HIDE_SELECTION_OUTLINE=1
        "${RENDERER}" "${SOURCE_DIR}/assets/scenes/${scene}.myscene"
        WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result TIMEOUT 90)
    if(NOT result EQUAL 0 OR NOT EXISTS "${OUTPUT_DIR}/${name}.png")
        message(FATAL_ERROR "Could not capture god rays ${name}")
    endif()
endfunction()
function(compare first second mae fraction expected)
    execute_process(COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/${first}.png"
        "${OUTPUT_DIR}/${second}.png" ${mae} ${fraction} RESULT_VARIABLE result)
    if(NOT result EQUAL expected)
        message(FATAL_ERROR "God rays comparison ${first}/${second} returned ${result}, expected ${expected}")
    endif()
endfunction()
capture(ground_off fixtures/27_cloud_lab_regression 0 0 high 8 172)
capture(ground_on fixtures/27_cloud_lab_regression 1 0 high 8 172)
capture(ground_deferred fixtures/27_cloud_lab_regression 1 1 high 8 172)
capture(water_off fixtures/23_ocean_clouds 0 1 high 8 172)
capture(water_on fixtures/23_ocean_clouds 1 1 high 8 172)
capture(water_low fixtures/23_ocean_clouds 1 1 low 8 172)
capture(water_repeat fixtures/23_ocean_clouds 1 1 high 8 172)
capture(night_off fixtures/23_ocean_clouds 0 1 high -10 172)
capture(night_on fixtures/23_ocean_clouds 1 1 high -10 172)
capture(behind_off fixtures/23_ocean_clouds 0 1 high 8 0)
capture(behind_on fixtures/23_ocean_clouds 1 1 high 8 0)
compare(ground_off ground_on 0.0001 0.001 1)
compare(water_off water_on 0.0001 0.001 1)
compare(ground_on ground_deferred 0.008 0.08 0)
compare(water_on water_low 0.035 0.25 0)
compare(water_on water_repeat 0 0 0)
compare(night_off night_on 0 0 0)
compare(behind_off behind_on 0 0 0)
message(STATUS "God rays visual acceptance: PASS")
