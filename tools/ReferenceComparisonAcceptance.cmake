cmake_minimum_required(VERSION 3.20)

foreach(required_variable RENDERER SOURCE_DIR BINARY_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

set(reference_width 256)
set(reference_height 256)
set(reference_spp 512)
set(reference_max_depth 8)
set(reference_seed 20260915)
set(reference_output_root "${BINARY_DIR}/path-tracing-raster-comparison")
if(DEFINED REFERENCE_WIDTH)
    set(reference_width "${REFERENCE_WIDTH}")
endif()
if(DEFINED REFERENCE_HEIGHT)
    set(reference_height "${REFERENCE_HEIGHT}")
endif()
if(DEFINED REFERENCE_SPP)
    set(reference_spp "${REFERENCE_SPP}")
endif()
if(DEFINED REFERENCE_MAX_DEPTH)
    set(reference_max_depth "${REFERENCE_MAX_DEPTH}")
endif()
if(DEFINED REFERENCE_SEED)
    set(reference_seed "${REFERENCE_SEED}")
endif()
if(DEFINED REFERENCE_OUTPUT_ROOT)
    set(reference_output_root "${REFERENCE_OUTPUT_ROOT}")
endif()

set(reference_scenes
    10_reference_pathtracer_pbr_hdri
    11_reference_pathtracer_lights
    12_reference_pathtracer_volume
)
set(required_artifacts
    raster.png
    path-traced.png
    difference-raw.png
    difference.png
    triptych.png
    comparison.json
    aov/path-traced.hdr
    aov/path-traced.png
    aov/path-traced-albedo.hdr
    aov/path-traced-albedo.png
    aov/path-traced-normal.hdr
    aov/path-traced-normal.png
    aov/path-traced-depth.hdr
    aov/path-traced-depth.png
    aov/path-traced-direct.hdr
    aov/path-traced-direct.png
    aov/path-traced-indirect.hdr
    aov/path-traced-indirect.png
    aov/path-traced-sample-count.hdr
    aov/path-traced-sample-count.png
    aov/path-traced-variance.hdr
    aov/path-traced-variance.png
)

file(MAKE_DIRECTORY "${reference_output_root}")
foreach(scene IN LISTS reference_scenes)
    set(scene_path "${SOURCE_DIR}/assets/scenes/fixtures/${scene}.myscene")
    set(output_directory "${reference_output_root}/${scene}")
    if(NOT EXISTS "${scene_path}")
        message(FATAL_ERROR "Reference scene is missing: ${scene_path}")
    endif()
    file(MAKE_DIRECTORY "${output_directory}")
    message(STATUS "Rendering ${scene}.myscene")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "MYRENDERER_SMOKE_TEST=1"
            "MYRENDERER_RENDER_WIDTH=${reference_width}"
            "MYRENDERER_RENDER_HEIGHT=${reference_height}"
            "MYRENDERER_REFERENCE_COMPARE_DIR=${output_directory}"
            "MYRENDERER_REFERENCE_SPP=${reference_spp}"
            "MYRENDERER_REFERENCE_MAX_DEPTH=${reference_max_depth}"
            "MYRENDERER_REFERENCE_SEED=${reference_seed}"
            "${RENDERER}"
            "${scene_path}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE render_result
    )
    if(NOT render_result EQUAL 0)
        message(FATAL_ERROR "Reference comparison failed for ${scene} (exit ${render_result})")
    endif()

    foreach(artifact IN LISTS required_artifacts)
        if(NOT EXISTS "${output_directory}/${artifact}")
            message(FATAL_ERROR "${scene} did not produce ${artifact}")
        endif()
    endforeach()

    file(READ "${output_directory}/comparison.json" comparison_report)
    foreach(expected_text
        "\"version\": 2"
        "\"scene\": \"${scene}.myscene\""
        "\"width\": ${reference_width}"
        "\"height\": ${reference_height}"
        "\"toneMapping\": \"aces-fitted\""
        "\"encoding\": \"srgb\""
        "\"displayFilter\": \"5x5-median\""
        "\"targetSamplesPerPixel\": ${reference_spp}"
        "\"maxDepth\": ${reference_max_depth}"
        "\"seed\": ${reference_seed}"
    )
        string(FIND "${comparison_report}" "${expected_text}" expected_position)
        if(expected_position EQUAL -1)
            message(FATAL_ERROR
                "${scene}/comparison.json is missing reproducibility field: ${expected_text}"
            )
        endif()
    endforeach()
endforeach()

message(STATUS "Reference comparison acceptance passed: ${reference_output_root}")
