cmake_minimum_required(VERSION 3.20)

foreach(required RENDERER COMPARATOR SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(scene "${SOURCE_DIR}/assets/scenes/fixtures/19_coastal_cascades.myscene")

function(capture name path count debug)
    set(image "${OUTPUT_DIR}/${name}.png")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_WIDTH=1280
            MYRENDERER_RENDER_HEIGHT=720
            MYRENDERER_SCREENSHOT=${image}
            MYRENDERER_RENDER_PATH=${path}
            MYRENDERER_SHADOW_CASCADES=${count}
            MYRENDERER_SHADOW_CASCADE_DEBUG=${debug}
            MYRENDERER_TAA=0
            MYRENDERER_BLOOM=0
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            "${RENDERER}" "${scene}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0 OR NOT EXISTS "${image}")
        message(FATAL_ERROR "Could not capture ${name}")
    endif()
    file(SIZE "${image}" size)
    if(size LESS 1024)
        message(FATAL_ERROR "Cascade capture is unexpectedly small: ${image}")
    endif()
endfunction()

function(require_different first second description)
    execute_process(
        COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/${first}.png" "${OUTPUT_DIR}/${second}.png" 0 0
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 1)
        message(FATAL_ERROR "${description} did not produce a distinct image (exit ${result})")
    endif()
endfunction()

function(require_similar first second description)
    execute_process(
        COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/${first}.png" "${OUTPUT_DIR}/${second}.png" 0.002 0.01
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${description} disagree beyond MAE 0.002 or 1% changed pixels (exit ${result})")
    endif()
endfunction()

capture(forward_final 0 3 0)
capture(forward_cascade_1 0 1 1)
capture(forward_cascade_3 0 3 1)
capture(deferred_final 1 3 0)
capture(deferred_cascade_1 1 1 1)
capture(deferred_cascade_3 1 3 1)
require_different(forward_final forward_cascade_3 "Forward debug view")
require_different(deferred_final deferred_cascade_3 "Deferred debug view")
require_different(forward_cascade_1 forward_cascade_3 "Forward cascade split")
require_different(deferred_cascade_1 deferred_cascade_3 "Deferred cascade split")
require_similar(forward_cascade_1 deferred_cascade_1 "Single-cascade Forward and Deferred regions")
require_similar(forward_cascade_3 deferred_cascade_3 "Three-cascade Forward and Deferred regions")

foreach(count 1 3 4)
    set(report "${OUTPUT_DIR}/cascade_${count}_benchmark.json")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            MYRENDERER_BENCHMARK_FRAMES=30
            MYRENDERER_BENCHMARK_WARMUP=8
            MYRENDERER_BENCHMARK_OUTPUT=${report}
            MYRENDERER_RENDER_WIDTH=1280
            MYRENDERER_RENDER_HEIGHT=720
            MYRENDERER_SHADOW_CASCADES=${count}
            MYRENDERER_TAA=0
            MYRENDERER_BLOOM=0
            MYRENDERER_HIDE_SELECTION_OUTLINE=1
            "${RENDERER}" "${scene}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0 OR NOT EXISTS "${report}")
        message(FATAL_ERROR "Could not benchmark ${count} cascade(s)")
    endif()
    file(READ "${report}" json)
    string(JSON shadow_p50 ERROR_VARIABLE parse_error GET "${json}" gpuPasses "Shadow maps" p50Ms)
    if(parse_error OR shadow_p50 LESS_EQUAL 0)
        message(FATAL_ERROR "Missing GPU shadow-pass timing in ${report}: ${parse_error}")
    endif()
    string(JSON frame_p50 GET "${json}" gpuFrameP50Ms)
    message(STATUS "${count} cascade(s): shadow GPU P50 ${shadow_p50} ms; frame GPU P50 ${frame_p50} ms")
endforeach()
