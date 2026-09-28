cmake_minimum_required(VERSION 3.20)

foreach(required RENDERER SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(scene "${SOURCE_DIR}/assets/scenes/fixtures/21_ocean_depth.myscene")

function(run_water_benchmark name enabled quality)
    set(report "${OUTPUT_DIR}/${name}.json")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_BENCHMARK_FRAMES=60
            MYRENDERER_BENCHMARK_WARMUP=16
            MYRENDERER_BENCHMARK_OUTPUT=${report}
            MYRENDERER_RENDER_WIDTH=1280
            MYRENDERER_RENDER_HEIGHT=720
            MYRENDERER_RENDER_PATH=1
            MYRENDERER_WATER=${enabled}
            MYRENDERER_WATER_PRESET=3
            MYRENDERER_WATER_QUALITY=${quality}
            MYRENDERER_ANIMATION_TIME=1.25
            MYRENDERER_TAA=0
            MYRENDERER_BLOOM=0
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            "${RENDERER}" "${scene}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0 OR NOT EXISTS "${report}")
        message(FATAL_ERROR "Could not benchmark ${name}")
    endif()
    file(READ "${report}" json)
    string(JSON pass_p50 ERROR_VARIABLE parse_error GET "${json}"
        gpuPasses "Forward transparent / refractive scene" p50Ms)
    if(parse_error OR pass_p50 LESS_EQUAL 0)
        message(FATAL_ERROR "Missing refractive GPU timing for ${name}: ${parse_error}")
    endif()
    string(JSON pass_p95 GET "${json}"
        gpuPasses "Forward transparent / refractive scene" p95Ms)
    string(JSON pass_samples GET "${json}"
        gpuPasses "Forward transparent / refractive scene" measurements)
    string(JSON frame_p50 GET "${json}" gpuFrameP50Ms)
    string(JSON frame_p95 GET "${json}" gpuFrameP95Ms)
    if(pass_samples LESS 30 OR pass_p95 GREATER 2.0 OR frame_p95 GREATER 8.0)
        message(FATAL_ERROR "${name} exceeded the 1280x720 GPU budget or lacks timing samples")
    endif()
    message(STATUS "${name}: refractive GPU P50/P95 ${pass_p50}/${pass_p95} ms; frame GPU P50/P95 ${frame_p50}/${frame_p95} ms")
endfunction()

run_water_benchmark(off 0 1)
run_water_benchmark(low 1 0)
run_water_benchmark(high 1 1)
