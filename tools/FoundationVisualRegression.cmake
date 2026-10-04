if(NOT DEFINED RENDERER OR NOT DEFINED COMPARATOR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FoundationVisualRegression requires RENDERER, COMPARATOR, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(capture_foundation name model scene_demo object_motion animation_demo animation_step taa_debug)
    set(output "${OUTPUT_DIR}/${name}.png")
    set(baseline "${SOURCE_DIR}/tests/baselines/images/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_SCREENSHOT=${output}
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            MYRENDERER_SCREENSHOT_WARMUP=4
            MYRENDERER_MSAA=1
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_TAA=${taa_debug}
            MYRENDERER_TAA_DEBUG=${taa_debug}
            MYRENDERER_SCENE_FOUNDATION_DEMO=${scene_demo}
            MYRENDERER_OBJECT_MOTION_DEMO=${object_motion}
            MYRENDERER_ANIMATION_DEMO=${animation_demo}
            MYRENDERER_ANIMATION=1
            MYRENDERER_ANIMATION_FRAME_STEP=${animation_step}
            MYRENDERER_PBR=1
            MYRENDERER_IBL=1
            MYRENDERER_SHADOWS=0
            MYRENDERER_BLOOM=0
            MYRENDERER_GRID=0
            MYRENDERER_AXES=0
            MYRENDERER_GROUND=${scene_demo}
            "${RENDERER}"
            "${SOURCE_DIR}/assets/models/${model}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0)
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    if(UPDATE_BASELINES)
        file(COPY_FILE "${output}" "${baseline}" ONLY_IF_DIFFERENT)
    elseif(NOT EXISTS "${baseline}")
        message(FATAL_ERROR "Missing SR-P0 baseline: ${baseline}")
    else()
        execute_process(
            COMMAND "${COMPARATOR}" "${baseline}" "${output}" 0.02 0.10
            RESULT_VARIABLE comparison_result
        )
        if(NOT comparison_result EQUAL 0)
            message(FATAL_ERROR "SR-P0 visual regression failed for ${name}")
        endif()
    endif()
endfunction()

capture_foundation(sr_p0_scene_entities cube.obj 1 0 0 0.0 0)
capture_foundation(sr_p0_object_motion cube.obj 0 1 0 0.0 1)
capture_foundation(sr_p0_skin_motion skinning_test.gltf 0 0 1 0.18 1)
