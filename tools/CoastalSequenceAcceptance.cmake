if(NOT DEFINED RENDERER OR NOT DEFINED JOB OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "RENDERER, JOB and BINARY_DIR are required")
endif()

set(root "${BINARY_DIR}/coastal-sequence-acceptance")
foreach(run IN ITEMS first second)
    set(directory "${root}/${run}")
    file(MAKE_DIRECTORY "${directory}")
    foreach(frame RANGE 0 12)
        if(frame LESS 10)
            set(padded "000${frame}")
        else()
            set(padded "00${frame}")
        endif()
        file(REMOVE "${directory}/frame_${padded}.png" "${directory}/frame_${padded}-report.json")
    endforeach()
    execute_process(
        COMMAND "${RENDERER}" raster-sequence "${JOB}" --output "${directory}/frame_{frame:04}"
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE output
        ERROR_VARIABLE errors
    )
    if(NOT exit_code EQUAL 0)
        message(FATAL_ERROR "Raster sequence ${run} failed (${exit_code}): ${output}\n${errors}")
    endif()
    file(GLOB frames "${directory}/frame_*.png")
    list(LENGTH frames count)
    if(NOT count EQUAL 13)
        message(FATAL_ERROR "Raster sequence ${run} produced ${count} frames instead of 13")
    endif()
endforeach()

file(SHA256 "${root}/first/frame_0000.png" noon)
file(SHA256 "${root}/first/frame_0009.png" sunset)
file(SHA256 "${root}/first/frame_0012.png" night)
if(noon STREQUAL sunset OR sunset STREQUAL night OR noon STREQUAL night)
    message(FATAL_ERROR "Noon, sunset and night frames must differ")
endif()
if(DEFINED COMPARATOR AND EXISTS "${COMPARATOR}")
    execute_process(
        COMMAND "${COMPARATOR}" --night-check "${root}/first/frame_0012.png"
        RESULT_VARIABLE night_check
        OUTPUT_VARIABLE night_metrics
        ERROR_VARIABLE night_errors
    )
    if(NOT night_check EQUAL 0)
        message(FATAL_ERROR "Night sky visibility check failed: ${night_metrics}\n${night_errors}")
    endif()
    message(STATUS "${night_metrics}")
endif()
foreach(frame RANGE 0 12)
    if(frame LESS 10)
        set(padded "000${frame}")
    else()
        set(padded "00${frame}")
    endif()
    file(SHA256 "${root}/first/frame_${padded}.png" first_hash)
    file(SHA256 "${root}/second/frame_${padded}.png" second_hash)
    if(NOT first_hash STREQUAL second_hash)
        message(FATAL_ERROR "Raster frame ${frame} changed across identical Render Job runs")
    endif()
endforeach()
message(STATUS "Coastal sequence PASS: 13 PNG frames, changing states, repeatable output")
