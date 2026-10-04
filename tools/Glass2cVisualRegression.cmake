if(NOT DEFINED RENDERER OR NOT DEFINED COMPARATOR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "Glass2cVisualRegression requires RENDERER, COMPARATOR, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(capture_and_compare name)
    set(output "${OUTPUT_DIR}/${name}.png")
    set(baseline "${SOURCE_DIR}/tests/baselines/images/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_SCREENSHOT=${output}
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            ${ARGN}
            "${RENDERER}"
            "${SOURCE_DIR}/assets/models/glass_volume_sphere.gltf"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0)
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    if(UPDATE_BASELINES)
        file(COPY_FILE "${output}" "${baseline}" ONLY_IF_DIFFERENT)
    else()
        execute_process(
            COMMAND "${COMPARATOR}" "${baseline}" "${output}" 0.015 0.08
            RESULT_VARIABLE comparison_result
        )
        if(NOT comparison_result EQUAL 0)
            message(FATAL_ERROR "Visual regression failed for ${name}")
        endif()
    endif()
endfunction()

capture_and_compare(glass2c_msaa1 MYRENDERER_MSAA=1)
capture_and_compare(glass2c_msaa4 MYRENDERER_MSAA=4)
capture_and_compare(
    glass2c_approximate
    MYRENDERER_MSAA=4
    MYRENDERER_TWO_INTERFACE_REFRACTION=0
)
capture_and_compare(glass2c_thickness MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=5)
capture_and_compare(glass2c_transmittance MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=6)
capture_and_compare(glass2c_exit_normal MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=9)
capture_and_compare(glass2c_object_id MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=10)
