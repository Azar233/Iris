if(NOT DEFINED RENDERER OR NOT DEFINED COMPARATOR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "LocalLightsVisualRegression requires RENDERER, COMPARATOR, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(compare_capture name)
    set(output "${OUTPUT_DIR}/${name}.png")
    set(baseline "${SOURCE_DIR}/tests/baselines/images/${name}.png")
    if(UPDATE_BASELINES)
        file(COPY_FILE "${output}" "${baseline}" ONLY_IF_DIFFERENT)
        return()
    endif()
    if(NOT EXISTS "${baseline}")
        message(FATAL_ERROR "Missing local-light baseline: ${baseline}")
    endif()
    execute_process(
        COMMAND "${COMPARATOR}" "${baseline}" "${output}" 0.015 0.08
        RESULT_VARIABLE comparison_result
    )
    if(NOT comparison_result EQUAL 0)
        message(FATAL_ERROR "Local-light visual regression failed for ${name}")
    endif()
endfunction()

function(capture_local_lights name render_path tier)
    set(output "${OUTPUT_DIR}/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_SCREENSHOT=${output}
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=${render_path}
            MYRENDERER_GBUFFER_DEBUG=0
            MYRENDERER_LIGHT_STRESS=1
            MYRENDERER_LOCAL_LIGHT_TIER=${tier}
            "${RENDERER}"
            "${SOURCE_DIR}/assets/models/cube.obj"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0)
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    compare_capture(${name})
endfunction()

capture_local_lights(gp_p1b_forward_lights8 0 0)
capture_local_lights(gp_p1b_deferred_lights8 1 0)
capture_local_lights(gp_p1b_forward_lights64 0 2)
capture_local_lights(gp_p1b_deferred_lights64 1 2)
