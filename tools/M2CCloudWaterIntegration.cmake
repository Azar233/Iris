cmake_minimum_required(VERSION 3.20)
foreach(required RENDERER COMPARATOR SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} required")
    endif()
endforeach()
foreach(name forward deferred shadow-off)
    if(name STREQUAL forward)
        set(path 0)
    else()
        set(path 1)
    endif()
    if(name STREQUAL shadow-off)
        set(shadows 0)
    else()
        set(shadows 1)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        MYRENDERER_BENCHMARK_FRAMES=60 MYRENDERER_BENCHMARK_WARMUP=16
        "MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/depth-cloud-${name}.json"
        "MYRENDERER_SCREENSHOT=${OUTPUT_DIR}/depth-cloud-${name}.png" MYRENDERER_SCREENSHOT_WARMUP=16
        MYRENDERER_RENDER_WIDTH=1280 MYRENDERER_RENDER_HEIGHT=720
        "MYRENDERER_RENDER_PATH=${path}" MYRENDERER_MSAA=4 MYRENDERER_TAA=0
        MYRENDERER_WATER_SURFACE_OPTICS=1 MYRENDERER_WATER_CLOUD_REFLECTION=1
        MYRENDERER_CLOUD_PRESET=cumulus MYRENDERER_CLOUDS=1 "MYRENDERER_CLOUD_SHADOWS=${shadows}"
        MYRENDERER_ANIMATION_TIME=1.25 MYRENDERER_ANIMATION_FRAME_STEP=0
        MYRENDERER_HIDE_SELECTION_OUTLINE=1 "MYRENDERER_RENDER_QUEUE_STATE=${OUTPUT_DIR}/queue.json"
        "${RENDERER}" "${SOURCE_DIR}/assets/scenes/fixtures/21_ocean_depth.myscene"
        WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 180)
    file(WRITE "${OUTPUT_DIR}/depth-cloud-${name}.log" "${out}\n${err}")
    if(NOT result EQUAL 0 OR NOT EXISTS "${OUTPUT_DIR}/depth-cloud-${name}.png")
        message(FATAL_ERROR "Cloud/water integration capture failed: ${name}")
    endif()
    file(READ "${OUTPUT_DIR}/depth-cloud-${name}.json" report)
    string(JSON optics GET "${report}" waterSurfaceOptics)
    string(JSON cloud_samples GET "${report}" gpuPasses "Cloud volume march" measurements)
    if(NOT optics OR cloud_samples LESS 30)
        message(FATAL_ERROR "Cloud and filtered water must both execute: ${name}")
    endif()
endforeach()
foreach(name forward shadow-off)
    if(name STREQUAL forward)
        set(mae 0.004)
        set(fraction 0.02)
        set(expected 0)
    else()
        set(mae 0.00001)
        set(fraction 0.00001)
        set(expected 1)
    endif()
    execute_process(COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/depth-cloud-${name}.png"
        "${OUTPUT_DIR}/depth-cloud-deferred.png" ${mae} ${fraction}
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    file(WRITE "${OUTPUT_DIR}/depth-cloud-${name}-comparison.txt" "${out}\n${err}")
    if(NOT result EQUAL expected)
        message(FATAL_ERROR "Cloud/water integration comparison failed: ${name}/${result}/${out}")
    endif()
endforeach()
file(WRITE "${OUTPUT_DIR}/cloud-water-integration.txt" "PASS: opaque contact geometry, cloud/water parity and cloud-shadow contribution\n")
