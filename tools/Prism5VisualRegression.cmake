if(NOT DEFINED RENDERER OR NOT DEFINED COMPARATOR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "Prism5VisualRegression requires RENDERER, COMPARATOR, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(capture_and_compare name)
    set(output "${OUTPUT_DIR}/${name}.png")
    set(baseline "${SOURCE_DIR}/tests/baselines/images/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_PRISM_DEMO=1
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_SCREENSHOT=${output}
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            ${ARGN}
            "${RENDERER}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0)
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    if(UPDATE_BASELINES)
        file(COPY_FILE "${output}" "${baseline}" ONLY_IF_DIFFERENT)
        return()
    endif()
    execute_process(
        COMMAND "${COMPARATOR}" "${baseline}" "${output}" 0.015 0.08
        RESULT_VARIABLE comparison_result
    )
    if(NOT comparison_result EQUAL 0)
        message(FATAL_ERROR "Visual regression failed for ${name}")
    endif()
endfunction()

capture_and_compare(
    prism5_white_beam_no_prism
    MYRENDERER_MSAA=4
    MYRENDERER_PRISM_SHOW_MODEL=0
    MYRENDERER_PRISM_IOR=1
    MYRENDERER_PRISM_DISPERSION=0
)
capture_and_compare(prism5_prism_no_dispersion MYRENDERER_MSAA=4 MYRENDERER_PRISM_DISPERSION=0)
capture_and_compare(prism5_continuous_21 MYRENDERER_MSAA=4 MYRENDERER_PRISM_SAMPLES=21)
capture_and_compare(prism5_seven_band MYRENDERER_MSAA=4 MYRENDERER_PRISM_SPECTRUM_MODE=seven)
capture_and_compare(prism5_tir_debug MYRENDERER_MSAA=4 MYRENDERER_PRISM_PRESET=2 MYRENDERER_PRISM_DEBUG=1)
capture_and_compare(prism5_angle_minus_8 MYRENDERER_MSAA=4 MYRENDERER_PRISM_BEAM_ANGLE=-8)
capture_and_compare(prism5_angle_plus_12 MYRENDERER_MSAA=4 MYRENDERER_PRISM_BEAM_ANGLE=12)
capture_and_compare(prism5_msaa1 MYRENDERER_MSAA=1)
capture_and_compare(prism5_msaa4 MYRENDERER_MSAA=4)
capture_and_compare(prism5_hero_exaggerated MYRENDERER_MSAA=4 MYRENDERER_PRISM_PRESET=3)
