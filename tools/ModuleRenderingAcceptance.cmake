# Accepts the P1-0C C++ module rendering path end to end:
#   simulate (dry run) -> bake (deterministic cache) -> render sequence
#   -> reproducible output across runs -> verified cache reuse
#   -> stale and missing cache are reported instead of silently reused.
#
# Every artifact is written under OUTPUT_DIR; no versioned baseline is touched.

foreach(required BATCH COMPARATOR RENDERER SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "ModuleRenderingAcceptance requires -D${required}=...")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

set(JOB_SOURCE "${SOURCE_DIR}/assets/renderjobs/03_cpu_turntable_module.renderjob")
if(NOT EXISTS "${JOB_SOURCE}")
    message(FATAL_ERROR "Missing module Render Job fixture: ${JOB_SOURCE}")
endif()
file(READ "${JOB_SOURCE}" JOB_TEXT)

# The fixture writes into the build tree; redirect every artifact under OUTPUT_DIR.
string(REPLACE "../../build-ci-msvc/render-jobs/03_cpu_turntable_module" "${OUTPUT_DIR}/run"
       JOB_TEXT "${JOB_TEXT}")
# The job file moves out of assets/renderjobs, so its relative scene path is rewritten
# to the absolute source path instead of depending on the new location.
string(REPLACE "\"scene\": \"../scenes/fixtures/01_multi_model_hierarchy.myscene\""
       "\"scene\": \"${SOURCE_DIR}/assets/scenes/fixtures/01_multi_model_hierarchy.myscene\""
       JOB_TEXT "${JOB_TEXT}")
set(JOB "${OUTPUT_DIR}/job.renderjob")
file(WRITE "${JOB}" "${JOB_TEXT}")

function(run_batch label)
    execute_process(
        COMMAND "${BATCH}" ${ARGN}
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${label} failed with ${result}\n${output}\n${error}")
    endif()
    set(LAST_BATCH_OUTPUT "${output}" PARENT_SCOPE)
endfunction()

function(require_file path label)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${label} is missing: ${path}")
    endif()
    file(SIZE "${path}" size)
    # The fixture renders 64x64 frames, so a depth AOV is legitimately a few hundred
    # bytes; the byte-for-byte comparisons below are the real correctness gate.
    if(size LESS 1)
        message(FATAL_ERROR "${label} is empty: ${path}")
    endif()
endfunction()

function(require_text path needle label)
    file(READ "${path}" text)
    string(FIND "${text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${label}: '${needle}' not found in ${path}")
    endif()
endfunction()

function(require_close left right max_mae max_changed label)
    execute_process(
        COMMAND "${COMPARATOR}" "${left}" "${right}" "${max_mae}" "${max_changed}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${label} is outside the accepted tolerance: ${output}${error}")
    endif()
endfunction()

# Runs the editor headlessly, has it export its CPU Path Traced preview once the target SPP
# is reached, and returns the exported PNG path in GUI_EXPORT_PNG.
function(export_gui_frame stem)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "MYRENDERER_CPU_PREVIEW=1"
                "MYRENDERER_CPU_PREVIEW_SCALE=3"
                "MYRENDERER_CPU_PREVIEW_SPP=${GUI_SPP}"
                "MYRENDERER_CPU_PREVIEW_DEPTH=${GUI_DEPTH}"
                "MYRENDERER_CPU_PREVIEW_SEED=${GUI_SEED}"
                "MYRENDERER_CPU_PREVIEW_AOV=0"
                "MYRENDERER_CPU_PREVIEW_POWER_LIGHTS=0"
                "MYRENDERER_CPU_PREVIEW_VNDF=0"
                "MYRENDERER_CPU_PREVIEW_DENOISE=0"
                "MYRENDERER_RENDER_WIDTH=64"
                "MYRENDERER_RENDER_HEIGHT=64"
                "MYRENDERER_TIMELINE_FRAME=${GUI_FRAME}"
                "MYRENDERER_MODULE=${GUI_MODULE}"
                "MYRENDERER_CPU_PREVIEW_EXPORT=${stem}"
                "${RENDERER}" "${SOURCE_DIR}/assets/scenes/fixtures/01_multi_model_hierarchy.myscene"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 600
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "GUI reference export failed with ${result}\n${output}\n${error}")
    endif()
    set("${stem}_png" "${stem}.png" PARENT_SCOPE)
endfunction()

function(require_identical left right label)
    execute_process(
        COMMAND "${COMPARATOR}" "${left}" "${right}" 0 0
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${label} is not byte-identical: ${output}${error}")
    endif()
endfunction()

# ---------------------------------------------------------------- validate
run_batch("validate" validate "${JOB}")
if(NOT LAST_BATCH_OUTPUT MATCHES "Valid Render Job schema 2")
    message(FATAL_ERROR "validate did not report schema 2 for a module job: ${LAST_BATCH_OUTPUT}")
endif()

# ------------------------------------------------------- simulate (dry run)
run_batch("simulate" simulate "${JOB}")
foreach(frame RANGE 0 23)
    string(FIND "${LAST_BATCH_OUTPUT}" "Frame ${frame} content " position)
    if(position EQUAL -1)
        message(FATAL_ERROR "simulate did not report frame ${frame}: ${LAST_BATCH_OUTPUT}")
    endif()
endforeach()
if(EXISTS "${OUTPUT_DIR}/run/simulation-cache.json")
    message(FATAL_ERROR "simulate must not write a simulation cache")
endif()

# ------------------------------------------------------------------- bake
run_batch("bake" bake "${JOB}")
set(CACHE_FILE "${OUTPUT_DIR}/run/simulation-cache.json")
require_file("${CACHE_FILE}" "Simulation cache")
require_text("${CACHE_FILE}" "MyRendererSimulationCache" "Simulation cache format")
require_text("${CACHE_FILE}" "\"schemaVersion\": 1" "Simulation cache schema")
require_text("${CACHE_FILE}" "\"buildId\": \"0.1.0+" "Simulation cache build id")
foreach(frame RANGE 0 23)
    require_text("${CACHE_FILE}" "\"frame\": ${frame}" "Simulation cache frame ${frame}")
endforeach()

# ------------------------------------------- render sequence with the cache
run_batch("render-sequence" render-sequence "${JOB}")
foreach(frame RANGE 0 23)
    string(SUBSTRING "0000${frame}" 0 4 padded)
    require_file("${OUTPUT_DIR}/run/frame_${padded}.png" "Frame ${frame} PNG")
    require_file("${OUTPUT_DIR}/run/frame_${padded}-depth.png" "Frame ${frame} depth AOV")
    require_file("${OUTPUT_DIR}/run/frame_${padded}-report.json" "Frame ${frame} report")
endforeach()
require_text("${OUTPUT_DIR}/run/frame_0002-report.json" "\"status\": \"Hit\"" "Cache reuse status")
require_text("${OUTPUT_DIR}/run/frame_0002-report.json"
             "\"id\": \"myrenderer.core.turntable\"" "Module manifest in the frame report")

# ------------------------------------------- the same job through --output
run_batch("render-sequence-override" render-sequence "${JOB}"
          --output "${OUTPUT_DIR}/repeat/frame_{frame:04}")
foreach(frame RANGE 0 23)
    string(SUBSTRING "0000${frame}" 0 4 padded)
    require_identical("${OUTPUT_DIR}/run/frame_${padded}.png"
                      "${OUTPUT_DIR}/repeat/frame_${padded}.png"
                      "Frame ${frame} across two runs")
endforeach()

# ------------------------------------------- a cache built from other inputs
# Same cache file, different module seed: the key no longer matches, so the frame must
# be simulated again and the mismatch reported instead of being reused silently.
string(REPLACE "\"seed\": 20260919,\n    \"parameters\"" "\"seed\": 4242,\n    \"parameters\""
       STALE_TEXT "${JOB_TEXT}")
string(REPLACE "\"${OUTPUT_DIR}/run/frame_{frame:04}\"" "\"${OUTPUT_DIR}/stale/frame_{frame:04}\""
       STALE_TEXT "${STALE_TEXT}")
set(STALE_JOB "${OUTPUT_DIR}/job-stale.renderjob")
file(WRITE "${STALE_JOB}" "${STALE_TEXT}")
if(STALE_TEXT STREQUAL JOB_TEXT)
    message(FATAL_ERROR "Could not build the stale-cache job fixture")
endif()
run_batch("stale" render-sequence "${STALE_JOB}")
require_file("${OUTPUT_DIR}/stale/frame_0000.png" "Stale-cache frame PNG")
require_text("${OUTPUT_DIR}/stale/frame_0000-report.json" "\"status\": \"Stale\""
             "Stale cache rejection")

# ------------------------------------------- a cache built from other parameters
# Same scene, seed, frame rate and build, only the module parameter set differs. The
# per-frame content hashes cannot detect this, so the parameter fingerprint is what makes
# the entry stale: the frame must be simulated again and the mismatch reported.
string(REPLACE "\"degreesPerFrame\": 15.0" "\"degreesPerFrame\": 30.0" PARAMETER_TEXT "${JOB_TEXT}")
string(REPLACE "\"${OUTPUT_DIR}/run/frame_{frame:04}\"" "\"${OUTPUT_DIR}/parameter/frame_{frame:04}\""
       PARAMETER_TEXT "${PARAMETER_TEXT}")
set(PARAMETER_JOB "${OUTPUT_DIR}/job-parameter.renderjob")
file(WRITE "${PARAMETER_JOB}" "${PARAMETER_TEXT}")
if(PARAMETER_TEXT STREQUAL JOB_TEXT)
    message(FATAL_ERROR "Could not build the parameter-cache job fixture")
endif()
run_batch("parameter" render-sequence "${PARAMETER_JOB}")
require_file("${OUTPUT_DIR}/parameter/frame_0000.png" "Parameter-cache frame PNG")
require_text("${OUTPUT_DIR}/parameter/frame_0000-report.json" "\"status\": \"Stale\""
             "Parameter change must invalidate the cache")
require_text("${OUTPUT_DIR}/parameter/frame_0000-report.json" "module parameters changed"
             "Parameter staleness reason")
# The parameter animation must actually change the rendered sequence.
execute_process(
    COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/run/frame_0012.png"
            "${OUTPUT_DIR}/parameter/frame_0012.png" 0 0
    RESULT_VARIABLE parameterComparison
)
if(parameterComparison EQUAL 0)
    message(FATAL_ERROR "A different module parameter set must change the rendered frame")
endif()

# ------------------------------------------------ a cache that is not there
file(REMOVE "${CACHE_FILE}")
run_batch("missing" render-sequence "${JOB}" --output "${OUTPUT_DIR}/missing/frame_{frame:04}")
require_text("${OUTPUT_DIR}/missing/frame_0000-report.json" "\"status\": \"Missing\""
             "Missing cache status")
foreach(frame RANGE 0 23)
    string(SUBSTRING "0000${frame}" 0 4 padded)
    require_identical("${OUTPUT_DIR}/run/frame_${padded}.png"
                      "${OUTPUT_DIR}/missing/frame_${padded}.png"
                      "Frame ${frame} without a cache")
endforeach()

# The fixture is a 24-frame parameter animation: 15 degrees per frame over 24 frames is
# exactly one revolution, so the first and last frame must differ while frame 0 of the
# second run stays byte-identical to the first run.
execute_process(
    COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/run/frame_0000.png"
            "${OUTPUT_DIR}/run/frame_0023.png" 0 0
    RESULT_VARIABLE revolutionComparison
)
if(revolutionComparison EQUAL 0)
    message(FATAL_ERROR "A 24-frame turntable revolution must change the rendered frame")
endif()
foreach(frame RANGE 0 23)
    string(SUBSTRING "0000${frame}" 0 4 padded)
    require_file("${OUTPUT_DIR}/run/frame_${padded}.png" "Frame ${frame} PNG")
    require_file("${OUTPUT_DIR}/run/frame_${padded}-depth.png" "Frame ${frame} depth AOV")
    require_file("${OUTPUT_DIR}/run/frame_${padded}-report.json" "Frame ${frame} report")
endforeach()

# ------------------------------------- GUI preview and batch agree frame for frame
# The editor preview and the headless batch runtime share the Timeline, the module runtime,
# the registry and the reference image writer, so the same frame has to come out the same.
#
# Without a module the two paths are byte-identical, which is the hard gate. With a module
# the GUI renders its runtime scene while the batch loader re-resolves the hierarchy from the
# scene document; that composition can differ in the last bit on a few boundary pixels, so
# the module frame is compared with a one-LSB tolerance (measured MAE ~5e-7, 0% changed).
set(GUI_SPP 2)
set(GUI_DEPTH 4)
set(GUI_SEED 20260917)
set(GUI_FRAME 0)
set(GUI_MODULE "")
run_batch("reference-frame" render-frame
          "${SOURCE_DIR}/assets/renderjobs/01_cpu_reference.renderjob" 0
          --output "${OUTPUT_DIR}/gui-parity/nomodule/reference")
export_gui_frame("${OUTPUT_DIR}/gui-parity/gui_nomodule")
require_file("${OUTPUT_DIR}/gui-parity/gui_nomodule.png" "GUI reference frame")
require_identical("${OUTPUT_DIR}/gui-parity/nomodule/reference.png"
                  "${OUTPUT_DIR}/gui-parity/gui_nomodule.png"
                  "GUI and batch reference frame")

set(GUI_SPP 4)
set(GUI_DEPTH 4)
set(GUI_SEED 20260919)
set(GUI_FRAME 12)
set(GUI_MODULE "myrenderer.core.turntable")
export_gui_frame("${OUTPUT_DIR}/gui-parity/gui_module_frame_0012")
require_file("${OUTPUT_DIR}/gui-parity/gui_module_frame_0012.png" "GUI module frame")
require_close("${OUTPUT_DIR}/run/frame_0012.png"
              "${OUTPUT_DIR}/gui-parity/gui_module_frame_0012.png"
              0.00001 0.0
              "GUI and batch module frame")

message(STATUS "Module rendering acceptance passed: ${OUTPUT_DIR}")
