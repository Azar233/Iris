if(NOT DEFINED RENDERER OR NOT DEFINED COMPARATOR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "SkinningVisualRegression requires RENDERER, COMPARATOR, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(capture_skinning name animation time debug_view)
    set(output "${OUTPUT_DIR}/${name}.png")
    set(baseline "${SOURCE_DIR}/tests/baselines/images/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_ANIMATION_DEMO=1
            MYRENDERER_ANIMATION=${animation}
            MYRENDERER_ANIMATION_TIME=${time}
            MYRENDERER_SKIN_DEBUG=${debug_view}
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_SCREENSHOT=${output}
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_TAA=0
            MYRENDERER_BLOOM=0
            "${RENDERER}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0)
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    if(UPDATE_BASELINES)
        file(COPY_FILE "${output}" "${baseline}" ONLY_IF_DIFFERENT)
    elseif(NOT EXISTS "${baseline}")
        message(FATAL_ERROR "Missing skinning baseline: ${baseline}")
    else()
        execute_process(
            COMMAND "${COMPARATOR}" "${baseline}" "${output}" 0.01 0.04
            RESULT_VARIABLE comparison_result
        )
        if(NOT comparison_result EQUAL 0)
            message(FATAL_ERROR "Skinning visual regression failed for ${name}")
        endif()
    endif()
endfunction()

capture_skinning(gp_p1e_bind_pose 0 0.0 0)
capture_skinning(gp_p1e_animated_pose 1 1.0 0)
capture_skinning(gp_p1e_joint_debug 1 1.0 1)
capture_skinning(gp_p1e_weight_debug 1 1.0 2)
