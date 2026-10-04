if(NOT DEFINED RENDERER OR NOT DEFINED COMPARATOR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "DeferredVisualRegression requires RENDERER, COMPARATOR, SOURCE_DIR, and OUTPUT_DIR")
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
        message(FATAL_ERROR "Missing Deferred baseline: ${baseline}")
    endif()
    execute_process(
        COMMAND "${COMPARATOR}" "${baseline}" "${output}" 0.015 0.08
        RESULT_VARIABLE comparison_result
    )
    if(NOT comparison_result EQUAL 0)
        message(FATAL_ERROR "Deferred visual regression failed for ${name}")
    endif()
endfunction()

function(capture_deferred name render_path debug_view)
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
            MYRENDERER_GBUFFER_DEBUG=${debug_view}
            MYRENDERER_PBR=1
            MYRENDERER_IBL=1
            MYRENDERER_SHADOWS=1
            MYRENDERER_BLOOM=1
            MYRENDERER_GRID=0
            MYRENDERER_AXES=0
            MYRENDERER_GROUND=1
            "${RENDERER}"
            "${SOURCE_DIR}/assets/models/pbr_material_test.gltf"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0)
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    compare_capture(${name})
endfunction()

# Same asset, camera, lighting, resolution, and 4x MSAA for path comparison.
capture_deferred(gp_p1_forward_final 0 0)
capture_deferred(gp_p1_deferred_final 1 0)

# Raw MRT attachment views bypass skybox, overlays, transparent composition, and post FX.
capture_deferred(gp_p1_gbuffer_albedo 1 1)
capture_deferred(gp_p1_gbuffer_normal 1 2)
capture_deferred(gp_p1_gbuffer_material 1 3)
capture_deferred(gp_p1_gbuffer_depth 1 4)
