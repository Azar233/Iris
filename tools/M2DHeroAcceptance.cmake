cmake_minimum_required(VERSION 3.20)
foreach(required RENDERER COMPARATOR CONTRACTS CALIBRATION SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT_DIR}/m2d-acceptance.txt" "RUNNING: final Hero gates pending\n")
include("${SOURCE_DIR}/tools/M2CWaterAcceptance.cmake")
execute_process(COMMAND "${CALIBRATION}" --hero "${hero}" "${OUTPUT_DIR}/cloud-shape"
    RESULT_VARIABLE shape OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 180)
file(WRITE "${OUTPUT_DIR}/cloud-shape.log" "${out}\n${err}")
if(NOT shape EQUAL 0)
    message(FATAL_ERROR "Final Hero Low/High cloud shape failed: ${out}/${err}")
endif()
# Coastal Sequence owns Water Enabled. Bake its actual frame-zero renderer state
# through the shared presentation/save path before making diagnostic On/Off copies.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    MYRENDERER_BENCHMARK_FRAMES=30 MYRENDERER_BENCHMARK_WARMUP=4
    "MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/bake-profile.json"
    "MYRENDERER_PRESENTATION_SNAPSHOT=${OUTPUT_DIR}/hero-frame-zero.myscene"
    MYRENDERER_TIMELINE_FRAME=0 MYRENDERER_ANIMATION_FRAME_STEP=0
    "MYRENDERER_RENDER_QUEUE_STATE=${OUTPUT_DIR}/queue.json"
    "${RENDERER}" "${hero}" WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE baked OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 180)
file(WRITE "${OUTPUT_DIR}/bake.log" "${out}\n${err}")
if(NOT baked EQUAL 0 OR NOT EXISTS "${OUTPUT_DIR}/hero-frame-zero.myscene")
    message(FATAL_ERROR "Could not bake actual Hero state for diagnostic captures")
endif()
file(READ "${OUTPUT_DIR}/hero-frame-zero.myscene" snapshot)
file(WRITE "${OUTPUT_DIR}/hero-diagnostic.myscene" "${snapshot}\n")
foreach(variant diagnostic-on water-off clouds-off)
    if(variant STREQUAL water-off)
        set(extra MYRENDERER_WATER=0)
    elseif(variant STREQUAL clouds-off)
        set(extra MYRENDERER_CLOUDS=0 MYRENDERER_CLOUD_SHADOWS=0)
    else()
        set(extra)
    endif()
    set(variant_scene "${OUTPUT_DIR}/hero-diagnostic.myscene")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        MYRENDERER_BENCHMARK_FRAMES=60 MYRENDERER_BENCHMARK_WARMUP=16
        "MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${variant}.json"
        "MYRENDERER_SCREENSHOT=${OUTPUT_DIR}/${variant}.png" MYRENDERER_SCREENSHOT_WARMUP=16
        MYRENDERER_RENDER_WIDTH=1280 MYRENDERER_RENDER_HEIGHT=720
        MYRENDERER_TIMELINE_FRAME=0 MYRENDERER_ANIMATION_FRAME_STEP=0
        MYRENDERER_HIDE_SELECTION_OUTLINE=1 "MYRENDERER_RENDER_QUEUE_STATE=${OUTPUT_DIR}/queue.json"
        ${extra} "${RENDERER}" "${variant_scene}" WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 180)
    file(WRITE "${OUTPUT_DIR}/${variant}.log" "${out}\n${err}")
    if(NOT result EQUAL 0 OR NOT EXISTS "${OUTPUT_DIR}/${variant}.png")
        message(FATAL_ERROR "Final Hero variant failed: ${variant}/${result}")
    endif()
    if(variant STREQUAL water-off)
        file(READ "${OUTPUT_DIR}/${variant}.json" variant_report)
        string(JSON enabled GET "${variant_report}" waterEnabled)
        if(enabled)
            message(FATAL_ERROR "Module must not overwrite the diagnostic Water Off input")
        endif()
    endif()
    if(variant STREQUAL diagnostic-on)
        set(mae 0.004)
        set(fraction 0.02)
        set(expected 0)
    else()
        set(mae 0.001)
        set(fraction 0.01)
        set(expected 1)
    endif()
    execute_process(COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/noon-high.png"
        "${OUTPUT_DIR}/${variant}.png" ${mae} ${fraction} RESULT_VARIABLE different OUTPUT_VARIABLE out)
    file(WRITE "${OUTPUT_DIR}/${variant}-comparison.txt" "${out}")
    if(NOT different EQUAL expected)
        message(FATAL_ERROR "Hero variant must change visible pixels: ${variant}")
    endif()
endforeach()
file(WRITE "${OUTPUT_DIR}/m2d-acceptance.txt"
    "PASS: final Hero frozen budgets, water/sky GPU contracts, integration, cloud shape/transmission debug, On/Off and deterministic repeat\n")
