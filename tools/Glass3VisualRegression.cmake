if(NOT DEFINED RENDERER OR NOT DEFINED COMPARATOR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "Glass3VisualRegression requires RENDERER, COMPARATOR, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(capture_and_compare name)
    set(output "${OUTPUT_DIR}/${name}.png")
    set(baseline "${SOURCE_DIR}/tests/baselines/images/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_GLASS3_DEMO=1
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_SCREENSHOT=${output}
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            MYRENDERER_GLASS_DEBUG=0
            MYRENDERER_CAUSTICS=1
            MYRENDERER_CAUSTICS_MODE=1
            MYRENDERER_TRANSMISSION_SHADOWS=1
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

capture_and_compare(glass3_lightspace_msaa1 MYRENDERER_MSAA=1 MYRENDERER_CAUSTICS_MODE=1)
capture_and_compare(glass3_lightspace_msaa4 MYRENDERER_MSAA=4 MYRENDERER_CAUSTICS_MODE=1)
capture_and_compare(glass3_caustics_off MYRENDERER_MSAA=4 MYRENDERER_CAUSTICS=0)
capture_and_compare(glass3_projector MYRENDERER_MSAA=4 MYRENDERER_CAUSTICS_MODE=0)
capture_and_compare(glass3_caustics_debug MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=11)
capture_and_compare(glass3_transmission_shadow MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=12)
