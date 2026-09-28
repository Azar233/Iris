cmake_minimum_required(VERSION 3.20)

foreach(required_variable RENDERER COMPARATOR SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
function(capture_stylized name stylized render_path outline taa width height scene_name)
    set(output "${OUTPUT_DIR}/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "MYRENDERER_SMOKE_TEST=1"
            "MYRENDERER_RENDER_WIDTH=${width}"
            "MYRENDERER_RENDER_HEIGHT=${height}"
            "MYRENDERER_SCREENSHOT=${output}"
            "MYRENDERER_STYLIZED=${stylized}"
            "MYRENDERER_STYLIZED_BANDS=3"
            "MYRENDERER_STYLIZED_OUTLINE=${outline}"
            "MYRENDERER_STYLIZED_OUTLINE_WIDTH=1.5"
            "MYRENDERER_RENDER_PATH=${render_path}"
            "MYRENDERER_TAA=${taa}"
            "MYRENDERER_HIDE_SELECTION_OUTLINE=1"
            ${ARGN}
            "${RENDERER}"
            "${SOURCE_DIR}/assets/scenes/fixtures/${scene_name}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE capture_result
    )
    if(NOT capture_result EQUAL 0 OR NOT EXISTS "${output}")
        message(FATAL_ERROR "Failed to capture ${name}")
    endif()
    file(SIZE "${output}" output_size)
    if(output_size LESS 1024)
        message(FATAL_ERROR "Stylized capture is unexpectedly small: ${output}")
    endif()
endfunction()

function(require_same first second description)
    execute_process(
        COMMAND "${COMPARATOR}" "${first}" "${second}" 0 0
        RESULT_VARIABLE comparison_result
    )
    if(NOT comparison_result EQUAL 0)
        message(FATAL_ERROR "${description} was not frame-stable")
    endif()
endfunction()

function(require_different first second description)
    execute_process(
        COMMAND "${COMPARATOR}" "${first}" "${second}" 0 0
        RESULT_VARIABLE comparison_result
    )
    if(comparison_result EQUAL 0)
        message(FATAL_ERROR "${description} produced identical images")
    elseif(NOT comparison_result EQUAL 1)
        message(FATAL_ERROR "${description} comparison failed with exit code ${comparison_result}")
    endif()
endfunction()

set(gallery 14_polyhaven_material_gallery.myscene)
capture_stylized(sr_p2a_pbr_forward 0 0 0 0 640 360 ${gallery})
capture_stylized(sr_p2b_toon_forward 1 0 1 0 640 360 ${gallery})
capture_stylized(sr_p2b_toon_deferred 1 1 1 0 640 360 ${gallery})
capture_stylized(sr_p2b_toon_deferred_no_outline 1 1 0 0 640 360 ${gallery})
capture_stylized(sr_p2b_toon_deferred_taa 1 1 1 1 640 360 ${gallery})
capture_stylized(sr_p2b_toon_deferred_no_outline_taa 1 1 0 1 640 360 ${gallery})
capture_stylized(sr_p2b_toon_deferred_960x540 1 1 1 0 960 540 ${gallery})
capture_stylized(sr_p2b_toon_deferred_no_outline_960x540 1 1 0 0 960 540 ${gallery})
capture_stylized(
    sr_p2c_dither_forward 1 0 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_DITHER=1"
    "MYRENDERER_STYLIZED_DITHER_STRENGTH=0.8"
)
capture_stylized(
    sr_p2c_dither_deferred 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_DITHER=1"
    "MYRENDERER_STYLIZED_DITHER_STRENGTH=0.8"
)
capture_stylized(
    sr_p2c_dither_deferred_taa 1 1 1 1 640 360 ${gallery}
    "MYRENDERER_STYLIZED_DITHER=1"
    "MYRENDERER_STYLIZED_DITHER_STRENGTH=0.8"
)
capture_stylized(
    sr_p2c_dither_debug 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_DITHER=1"
    "MYRENDERER_STYLIZED_DEBUG=4"
)
capture_stylized(
    sr_p2c_dither_stability_early 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_DITHER=1"
    "MYRENDERER_STYLIZED_DITHER_STRENGTH=0.8"
    "MYRENDERER_SCREENSHOT_WARMUP=1"
)
capture_stylized(
    sr_p2c_dither_stability_late 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_DITHER=1"
    "MYRENDERER_STYLIZED_DITHER_STRENGTH=0.8"
    "MYRENDERER_SCREENSHOT_WARMUP=4"
)
capture_stylized(
    sr_p2c_fog_forward 1 0 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_FOG=1"
    "MYRENDERER_STYLIZED_FOG_DENSITY=0.55"
    "MYRENDERER_STYLIZED_FOG_BASE_HEIGHT=-0.4"
    "MYRENDERER_STYLIZED_FOG_FALLOFF=0.9"
)
capture_stylized(
    sr_p2c_fog_deferred 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_FOG=1"
    "MYRENDERER_STYLIZED_FOG_DENSITY=0.55"
    "MYRENDERER_STYLIZED_FOG_BASE_HEIGHT=-0.4"
    "MYRENDERER_STYLIZED_FOG_FALLOFF=0.9"
)
capture_stylized(
    sr_p2c_fog_debug 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_FOG=1"
    "MYRENDERER_STYLIZED_FOG_DENSITY=0.55"
    "MYRENDERER_STYLIZED_DEBUG=5"
)
capture_stylized(
    sr_p2b_transmission_boundary 1 1 1 0 640 360
    12_reference_pathtracer_volume.myscene
)
capture_stylized(
    sr_p2c_fog_transmission_boundary 1 1 1 0 640 360
    12_reference_pathtracer_volume.myscene
    "MYRENDERER_STYLIZED_FOG=1"
    "MYRENDERER_STYLIZED_FOG_DENSITY=0.55"
)
capture_stylized(
    sr_p2c_lut_clean_forward 1 0 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_COLOR_GRADING=1"
    "MYRENDERER_STYLIZED_LUT=0"
)
capture_stylized(
    sr_p2c_lut_clean_deferred 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_COLOR_GRADING=1"
    "MYRENDERER_STYLIZED_LUT=0"
)
capture_stylized(
    sr_p2c_lut_painterly 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_COLOR_GRADING=1"
    "MYRENDERER_STYLIZED_LUT=1"
)
capture_stylized(
    sr_p2c_lut_night_aurora 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_COLOR_GRADING=1"
    "MYRENDERER_STYLIZED_LUT=2"
)
capture_stylized(
    sr_p2c_lut_debug 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_COLOR_GRADING=1"
    "MYRENDERER_STYLIZED_LUT=2"
    "MYRENDERER_STYLIZED_DEBUG=6"
)
capture_stylized(
    sr_p2c_same_camera_clean_toon 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_PRESET=1"
)
capture_stylized(
    sr_p2c_same_camera_painterly 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_PRESET=2"
    "MYRENDERER_STYLIZED_BANDS=4"
)
capture_stylized(
    sr_p2c_same_camera_night_aurora 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_PRESET=3"
)
capture_stylized(
    sr_p2c_preset_clean_gallery 1 1 1 0 640 360
    15_stylized_clean_toon_gallery.myscene
    "MYRENDERER_STYLIZED_PRESET=1"
)
capture_stylized(
    sr_p2c_preset_painterly_interior 1 1 1 0 640 360
    16_stylized_painterly_interior.myscene
    "MYRENDERER_STYLIZED_PRESET=2"
    "MYRENDERER_STYLIZED_BANDS=4"
)
capture_stylized(
    sr_p2c_preset_night_aurora_outdoor 1 1 1 0 640 360
    17_stylized_night_aurora_outdoor.myscene
    "MYRENDERER_STYLIZED_PRESET=3"
)
capture_stylized(
    sr_p2c_debug_lighting_bands 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_DEBUG=1"
)
capture_stylized(
    sr_p2c_debug_rim 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_DEBUG=2"
)
capture_stylized(
    sr_p2c_debug_outline 1 1 1 0 640 360 ${gallery}
    "MYRENDERER_STYLIZED_DEBUG=3"
)

# The mode switch must be visually meaningful, while Forward and Deferred
# should remain close enough to represent the same stylized lighting model.
require_different(
    "${OUTPUT_DIR}/sr_p2a_pbr_forward.png"
    "${OUTPUT_DIR}/sr_p2b_toon_forward.png"
    "PBR/Stylized mode switch"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_toon_deferred_no_outline.png"
    "${OUTPUT_DIR}/sr_p2b_toon_deferred.png"
    "640x360 outline"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_toon_deferred_no_outline_960x540.png"
    "${OUTPUT_DIR}/sr_p2b_toon_deferred_960x540.png"
    "960x540 outline"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_toon_deferred_no_outline_taa.png"
    "${OUTPUT_DIR}/sr_p2b_toon_deferred_taa.png"
    "TAA outline"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_toon_deferred.png"
    "${OUTPUT_DIR}/sr_p2c_dither_deferred.png"
    "ordered dither"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_toon_deferred_taa.png"
    "${OUTPUT_DIR}/sr_p2c_dither_deferred_taa.png"
    "ordered dither with TAA"
)
require_different(
    "${OUTPUT_DIR}/sr_p2c_dither_deferred.png"
    "${OUTPUT_DIR}/sr_p2c_dither_debug.png"
    "dither debug view"
)
require_same(
    "${OUTPUT_DIR}/sr_p2c_dither_stability_early.png"
    "${OUTPUT_DIR}/sr_p2c_dither_stability_late.png"
    "ordered dither"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_toon_deferred.png"
    "${OUTPUT_DIR}/sr_p2c_fog_deferred.png"
    "height fog"
)
require_different(
    "${OUTPUT_DIR}/sr_p2c_fog_deferred.png"
    "${OUTPUT_DIR}/sr_p2c_fog_debug.png"
    "height-fog debug view"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_transmission_boundary.png"
    "${OUTPUT_DIR}/sr_p2c_fog_transmission_boundary.png"
    "height fog across the transparent boundary"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_toon_deferred.png"
    "${OUTPUT_DIR}/sr_p2c_lut_clean_deferred.png"
    "Clean Toon color-grading LUT"
)
require_different(
    "${OUTPUT_DIR}/sr_p2c_lut_clean_deferred.png"
    "${OUTPUT_DIR}/sr_p2c_lut_painterly.png"
    "Clean Toon/Painterly LUT selection"
)
require_different(
    "${OUTPUT_DIR}/sr_p2c_lut_painterly.png"
    "${OUTPUT_DIR}/sr_p2c_lut_night_aurora.png"
    "Painterly/Night Aurora LUT selection"
)
require_different(
    "${OUTPUT_DIR}/sr_p2c_lut_night_aurora.png"
    "${OUTPUT_DIR}/sr_p2c_lut_debug.png"
    "color-grading debug view"
)
require_different(
    "${OUTPUT_DIR}/sr_p2a_pbr_forward.png"
    "${OUTPUT_DIR}/sr_p2c_same_camera_clean_toon.png"
    "PBR/Clean Toon preset switch"
)
require_different(
    "${OUTPUT_DIR}/sr_p2c_same_camera_clean_toon.png"
    "${OUTPUT_DIR}/sr_p2c_same_camera_painterly.png"
    "Clean Toon/Painterly preset switch"
)
require_different(
    "${OUTPUT_DIR}/sr_p2c_same_camera_painterly.png"
    "${OUTPUT_DIR}/sr_p2c_same_camera_night_aurora.png"
    "Painterly/Night Aurora preset switch"
)
require_different(
    "${OUTPUT_DIR}/sr_p2b_toon_deferred.png"
    "${OUTPUT_DIR}/sr_p2c_debug_lighting_bands.png"
    "lighting-bands debug view"
)
require_different(
    "${OUTPUT_DIR}/sr_p2c_debug_lighting_bands.png"
    "${OUTPUT_DIR}/sr_p2c_debug_rim.png"
    "lighting-bands/rim debug views"
)
require_different(
    "${OUTPUT_DIR}/sr_p2c_debug_rim.png"
    "${OUTPUT_DIR}/sr_p2c_debug_outline.png"
    "rim/outline debug views"
)

execute_process(
    COMMAND "${COMPARATOR}"
        "${OUTPUT_DIR}/sr_p2b_toon_forward.png"
        "${OUTPUT_DIR}/sr_p2b_toon_deferred.png"
        0.02 0.15
    RESULT_VARIABLE path_comparison_result
)
if(NOT path_comparison_result EQUAL 0)
    message(FATAL_ERROR "Forward and Deferred stylized output diverged")
endif()

execute_process(
    COMMAND "${COMPARATOR}"
        "${OUTPUT_DIR}/sr_p2c_dither_forward.png"
        "${OUTPUT_DIR}/sr_p2c_dither_deferred.png"
        0.02 0.15
    RESULT_VARIABLE dither_path_comparison_result
)
if(NOT dither_path_comparison_result EQUAL 0)
    message(FATAL_ERROR "Forward and Deferred dithered output diverged")
endif()

execute_process(
    COMMAND "${COMPARATOR}"
        "${OUTPUT_DIR}/sr_p2c_fog_forward.png"
        "${OUTPUT_DIR}/sr_p2c_fog_deferred.png"
        0.02 0.15
    RESULT_VARIABLE fog_path_comparison_result
)
if(NOT fog_path_comparison_result EQUAL 0)
    message(FATAL_ERROR "Forward and Deferred height-fog output diverged")
endif()

execute_process(
    COMMAND "${COMPARATOR}"
        "${OUTPUT_DIR}/sr_p2c_lut_clean_forward.png"
        "${OUTPUT_DIR}/sr_p2c_lut_clean_deferred.png"
        0.02 0.15
    RESULT_VARIABLE lut_path_comparison_result
)
if(NOT lut_path_comparison_result EQUAL 0)
    message(FATAL_ERROR "Forward and Deferred color-graded output diverged")
endif()

message(STATUS "SR-P2A-C stylized effects, debug views, and three presets passed: ${OUTPUT_DIR}")
