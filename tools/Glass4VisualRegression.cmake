if(NOT DEFINED RENDERER OR NOT DEFINED COMPARATOR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "Glass4VisualRegression requires RENDERER, COMPARATOR, SOURCE_DIR, and OUTPUT_DIR")
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
        message(FATAL_ERROR "Missing Glass-4 baseline: ${baseline}")
    endif()
    execute_process(
        COMMAND "${COMPARATOR}" "${baseline}" "${output}" 0.015 0.08
        RESULT_VARIABLE comparison_result
    )
    if(NOT comparison_result EQUAL 0)
        message(FATAL_ERROR "Visual regression failed for ${name}")
    endif()
endfunction()

function(capture_volume name)
    set(output "${OUTPUT_DIR}/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_GLASS3_DEMO=0
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_SCREENSHOT=${output}
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            MYRENDERER_MSAA=4
            MYRENDERER_TRANSMISSION=1
            MYRENDERER_GEOMETRIC_THICKNESS=1
            MYRENDERER_TWO_INTERFACE_REFRACTION=1
            MYRENDERER_VOLUME_THICKNESS_SCALE=1.0
            MYRENDERER_GLASS_PRESET=1
            MYRENDERER_IOR=1.5
            MYRENDERER_DISPERSION_ENABLED=0
            MYRENDERER_DISPERSION=0
            MYRENDERER_CAUSTICS=0
            MYRENDERER_GLASS_DEBUG=0
            ${ARGN}
            "${RENDERER}"
            "${SOURCE_DIR}/assets/models/glass_volume_sphere.gltf"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0)
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    compare_capture(${name})
endfunction()

function(capture_caustics name)
    set(output "${OUTPUT_DIR}/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_GLASS3_DEMO=1
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_WIDTH=1920
            MYRENDERER_RENDER_HEIGHT=1080
            MYRENDERER_SCREENSHOT=${output}
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            MYRENDERER_MSAA=4
            MYRENDERER_TRANSMISSION=1
            MYRENDERER_GEOMETRIC_THICKNESS=1
            MYRENDERER_TWO_INTERFACE_REFRACTION=1
            MYRENDERER_VOLUME_THICKNESS_SCALE=1.0
            MYRENDERER_GLASS_PRESET=3
            MYRENDERER_IOR=1.5
            MYRENDERER_DISPERSION_ENABLED=1
            MYRENDERER_DISPERSION=2.0
            MYRENDERER_CAUSTICS=1
            MYRENDERER_CAUSTICS_MODE=1
            MYRENDERER_GLASS_DEBUG=0
            ${ARGN}
            "${RENDERER}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0)
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    compare_capture(${name})
endfunction()

# KHR_materials_volume validation scene: same camera, isolated variables.
capture_volume(glass4_volume_final)
capture_volume(glass4_volume_glass_off MYRENDERER_TRANSMISSION=0)
capture_volume(glass4_volume_ior_low MYRENDERER_IOR=1.1)
capture_volume(glass4_volume_ior_high MYRENDERER_IOR=1.8)
capture_volume(glass4_volume_thickness_low MYRENDERER_VOLUME_THICKNESS_SCALE=0.35)
capture_volume(glass4_volume_attenuation_clear MYRENDERER_GLASS_PRESET=0)
capture_volume(glass4_volume_exit_normal MYRENDERER_GLASS_DEBUG=9)
capture_volume(glass4_volume_object_id MYRENDERER_GLASS_DEBUG=10)
capture_volume(glass4_volume_approximate MYRENDERER_TWO_INTERFACE_REFRACTION=0)
capture_volume(glass4_volume_msaa1 MYRENDERER_MSAA=1)

# Crystal caustics hero: independent Dispersion and Caustics toggles, same camera.
capture_caustics(glass4_caustics_final)
capture_caustics(glass4_caustics_dispersion_off MYRENDERER_DISPERSION_ENABLED=0)
capture_caustics(glass4_caustics_off MYRENDERER_CAUSTICS=0)
capture_caustics(glass4_caustics_msaa1 MYRENDERER_MSAA=1)
