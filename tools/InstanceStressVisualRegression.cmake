if(NOT DEFINED RENDERER OR NOT DEFINED COMPARATOR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "InstanceStressVisualRegression requires RENDERER, COMPARATOR, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(capture_instance_stress name optimization culling lod)
    set(output "${OUTPUT_DIR}/${name}.png")
    set(baseline "${SOURCE_DIR}/tests/baselines/images/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_SCREENSHOT=${output}
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            MYRENDERER_MSAA=4
            MYRENDERER_RENDER_PATH=0
            MYRENDERER_INSTANCE_STRESS=1
            MYRENDERER_INSTANCE_OPTIMIZATION=${optimization}
            MYRENDERER_FRUSTUM_CULLING=${culling}
            MYRENDERER_LOD=${lod}
            "${RENDERER}"
            "${SOURCE_DIR}/assets/models/sphere.obj"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0)
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    if(UPDATE_BASELINES)
        file(COPY_FILE "${output}" "${baseline}" ONLY_IF_DIFFERENT)
    elseif(NOT EXISTS "${baseline}")
        message(FATAL_ERROR "Missing instance stress baseline: ${baseline}")
    else()
        execute_process(
            COMMAND "${COMPARATOR}" "${baseline}" "${output}" 0.015 0.08
            RESULT_VARIABLE comparison_result
        )
        if(NOT comparison_result EQUAL 0)
            message(FATAL_ERROR "Instance stress visual regression failed for ${name}")
        endif()
    endif()
endfunction()

capture_instance_stress(gp_p1c_baseline 0 0 0)
capture_instance_stress(gp_p1c_optimized 1 1 1)
