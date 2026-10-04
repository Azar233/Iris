if(NOT DEFINED RENDERER OR NOT DEFINED COMPARATOR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "ScreenSpaceVisualRegression requires RENDERER, COMPARATOR, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(capture_screen_space name ssao taa gbuffer_debug taa_debug motion)
    set(output "${OUTPUT_DIR}/${name}.png")
    set(baseline "${SOURCE_DIR}/tests/baselines/images/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_SCREENSHOT=${output}
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            MYRENDERER_SCREENSHOT_WARMUP=3
            MYRENDERER_MSAA=1
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_GBUFFER_DEBUG=${gbuffer_debug}
            MYRENDERER_SSAO=${ssao}
            MYRENDERER_TAA=${taa}
            MYRENDERER_TAA_DEBUG=${taa_debug}
            MYRENDERER_TAA_MOTION_DEMO=${motion}
            MYRENDERER_PBR=1
            MYRENDERER_IBL=1
            MYRENDERER_SHADOWS=1
            MYRENDERER_BLOOM=0
            MYRENDERER_GRID=0
            MYRENDERER_AXES=0
            MYRENDERER_GROUND=1
            MYRENDERER_SCENE_DEMO=1
            "${RENDERER}"
            "${SOURCE_DIR}/assets/models/pbr_material_test.gltf"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0)
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    if(UPDATE_BASELINES)
        file(COPY_FILE "${output}" "${baseline}" ONLY_IF_DIFFERENT)
    elseif(NOT EXISTS "${baseline}")
        message(FATAL_ERROR "Missing screen-space baseline: ${baseline}")
    else()
        execute_process(
            COMMAND "${COMPARATOR}" "${baseline}" "${output}" 0.02 0.10
            RESULT_VARIABLE comparison_result
        )
        if(NOT comparison_result EQUAL 0)
            message(FATAL_ERROR "Screen-space visual regression failed for ${name}")
        endif()
    endif()
endfunction()

capture_screen_space(gp_p1d_baseline 0 0 0 0 0)
capture_screen_space(gp_p1d_ssao_final 1 0 0 0 0)
capture_screen_space(gp_p1d_ssao_debug 1 0 5 0 0)
capture_screen_space(gp_p1d_taa_static 0 1 0 0 0)
capture_screen_space(gp_p1d_taa_motion 0 1 0 0 1)
capture_screen_space(gp_p1d_motion_vectors 0 1 0 1 1)
capture_screen_space(gp_p1d_history_weight 0 1 0 2 1)
