cmake_minimum_required(VERSION 3.20)

foreach(required RENDERER COMPARATOR SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(scene "${SOURCE_DIR}/assets/scenes/fixtures/20_ocean_synthesis.myscene")

function(capture name render_path enabled time step taa debug)
    set(image "${OUTPUT_DIR}/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_WIDTH=960
            MYRENDERER_RENDER_HEIGHT=540
            MYRENDERER_SCREENSHOT=${image}
            MYRENDERER_RENDER_PATH=${render_path}
            MYRENDERER_WATER=${enabled}
            MYRENDERER_ANIMATION_TIME=${time}
            MYRENDERER_ANIMATION_FRAME_STEP=${step}
            MYRENDERER_TAA=${taa}
            MYRENDERER_TAA_DEBUG=${debug}
            MYRENDERER_BLOOM=0
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            "${RENDERER}" "${scene}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0 OR NOT EXISTS "${image}")
        message(FATAL_ERROR "Could not capture ${name}")
    endif()
    file(SIZE "${image}" size)
    if(size LESS 1024)
        message(FATAL_ERROR "Water capture is unexpectedly small: ${image}")
    endif()
endfunction()

function(compare first second mae fraction expected description)
    execute_process(
        COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/${first}.png" "${OUTPUT_DIR}/${second}.png"
            ${mae} ${fraction}
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL expected)
        message(FATAL_ERROR "${description}: comparator returned ${result}, expected ${expected}")
    endif()
endfunction()

capture(forward_off 0 0 0 0 0 0)
capture(forward_t0 0 1 0 0 0 0)
capture(forward_t1 0 1 1.25 0 0 0)
capture(deferred_off 1 0 0 0 0 0)
capture(deferred_t0 1 1 0 0 0 0)
capture(deferred_t1 1 1 1.25 0 0 0)
compare(forward_off forward_t0 0.001 0.01 1 "Forward water On/Off")
compare(deferred_off deferred_t0 0.001 0.01 1 "Deferred water On/Off")
compare(forward_t0 forward_t1 0.001 0.01 1 "Forward wave time evolution")
compare(deferred_t0 deferred_t1 0.001 0.01 1 "Deferred wave time evolution")
compare(forward_t1 deferred_t1 0.004 0.02 0 "Forward/Deferred water parity")

capture(motion_static 1 1 0 0 1 1)
capture(motion_animated 1 1 0 0.033333 1 1)
compare(motion_static motion_animated 0.001 0.01 1 "Water motion-vector output")

function(capture_variant name scene enabled preset quality)
    set(image "${OUTPUT_DIR}/${name}.png")
    set(foam_argument)
    if(ARGC GREATER 5)
        set(foam_argument "MYRENDERER_WATER_FOAM=${ARGV5}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_WIDTH=960
            MYRENDERER_RENDER_HEIGHT=540
            MYRENDERER_SCREENSHOT=${image}
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_WATER=${enabled}
            MYRENDERER_WATER_PRESET=${preset}
            MYRENDERER_WATER_QUALITY=${quality}
            ${foam_argument}
            MYRENDERER_ANIMATION_TIME=1.25
            MYRENDERER_TAA=0
            MYRENDERER_BLOOM=0
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            "${RENDERER}" "${SOURCE_DIR}/assets/scenes/fixtures/${scene}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0 OR NOT EXISTS "${image}")
        message(FATAL_ERROR "Could not capture ${name}")
    endif()
endfunction()

capture_variant(depth_off 21_ocean_depth.myscene 0 0 1)
capture_variant(depth_on 21_ocean_depth.myscene 1 0 1)
capture_variant(underwater_off 22_ocean_underwater.myscene 0 0 1)
capture_variant(underwater_on 22_ocean_underwater.myscene 1 0 1)
capture_variant(calm 20_ocean_synthesis.myscene 1 1 1)
capture_variant(windy 20_ocean_synthesis.myscene 1 2 1)
capture_variant(storm_high 20_ocean_synthesis.myscene 1 3 1)
capture_variant(storm_low 20_ocean_synthesis.myscene 1 3 0)
capture_variant(depth_foam_off 21_ocean_depth.myscene 1 0 1 0)
capture_variant(storm_foam_off 20_ocean_synthesis.myscene 1 3 1 0)
compare(forward_t1 depth_on 0.002 0.01 1 "Seabed must be visible through water")
compare(depth_off depth_on 0.01 0.05 1 "Water must attenuate the real seabed")
compare(underwater_off underwater_on 0.01 0.05 1 "Underwater fog")
compare(calm windy 0.002 0.05 1 "Calm/Windy sea states")
compare(windy storm_high 0.002 0.05 1 "Windy/Storm sea states")
compare(storm_low storm_high 0.04 0.40 0 "Low/High sea-state continuity")
compare(depth_foam_off depth_on 0.0002 0.002 1 "Shoreline foam contribution")
compare(storm_foam_off storm_high 0.0002 0.002 1 "Whitecap contribution")
