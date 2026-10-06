# M1-A: shared GUI save/reopen, module preview and explicit CPU Job parity.
foreach(required BATCH RENDERER COMPARATOR SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "WorkflowAcceptance requires ${required}")
    endif()
endforeach()
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef RUN_ID)
set(RUN "${OUTPUT_DIR}/run-${RUN_ID}")
file(MAKE_DIRECTORY "${RUN}")
set(SAVED "${RUN}/saved.myscene")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    MYRENDERER_CPU_PREVIEW=1 MYRENDERER_CPU_PREVIEW_SCALE=3
    MYRENDERER_CPU_PREVIEW_SPP=4 MYRENDERER_CPU_PREVIEW_DEPTH=4
    MYRENDERER_CPU_PREVIEW_SEED=20260919 MYRENDERER_CPU_PREVIEW_AOV=0
    MYRENDERER_CPU_PREVIEW_POWER_LIGHTS=0 MYRENDERER_CPU_PREVIEW_VNDF=0
    MYRENDERER_CPU_PREVIEW_DENOISE=0 MYRENDERER_RENDER_WIDTH=64 MYRENDERER_RENDER_HEIGHT=64
    MYRENDERER_TIMELINE_FRAME=12 "MYRENDERER_SCENE_ROUNDTRIP=${SAVED}"
    "MYRENDERER_MODULE_REPORT=${RUN}/gui-report.json"
    "MYRENDERER_CPU_PREVIEW_EXPORT=${RUN}/gui"
    "${RENDERER}" "${SOURCE_DIR}/assets/scenes/fixtures/26_module_workflow.myscene"
    WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result TIMEOUT 180)
if(NOT result EQUAL 0 OR NOT EXISTS "${SAVED}")
    message(FATAL_ERROR "GUI save/reopen/export failed: ${result}")
endif()
file(READ "${SAVED}" SCENE)
file(READ "${SOURCE_DIR}/assets/renderjobs/05_module_workflow.renderjob" JOB)
string(JSON JOB SET "${JOB}" scene "\"${SAVED}\"")
string(JSON JOB SET "${JOB}" output path "\"${RUN}/batch/frame_{frame:04}\"")
string(JSON JOB SET "${JOB}" output resume false)
# The saved Scene and the authored Job must describe the same non-default parameters.
string(JSON count LENGTH "${SCENE}" module parameters)
if(count LESS 2)
    message(FATAL_ERROR "Saved Scene lost its non-default module parameters")
endif()
math(EXPR last "${count}-1")
foreach(index RANGE 0 ${last})
    string(JSON id GET "${SCENE}" module parameters ${index} id)
    string(JSON value GET "${SCENE}" module parameters ${index} value)
    string(JSON expected GET "${JOB}" module parameters "${id}")
    if(NOT value STREQUAL expected)
        message(FATAL_ERROR "Saved Scene/Job parameter mismatch: ${id}: ${value} != ${expected}")
    endif()
endforeach()
file(WRITE "${RUN}/job.renderjob" "${JOB}")
execute_process(COMMAND "${BATCH}" render-frame "${RUN}/job.renderjob" 12
    WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result TIMEOUT 180)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Batch frame failed: ${result}")
endif()
execute_process(COMMAND "${COMPARATOR}" "${RUN}/gui.png" "${RUN}/batch/frame_0012.png" 0.00001 0.0
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "GUI/Batch saved Scene image parity failed")
endif()
file(READ "${RUN}/gui-report.json" GUI)
file(READ "${RUN}/batch/frame_0012-report.json" BATCH_REPORT)
foreach(field id apiVersion buildId seed lastFrame)
    string(JSON guiValue GET "${GUI}" module ${field})
    string(JSON batchValue GET "${BATCH_REPORT}" module ${field})
    if(NOT guiValue STREQUAL batchValue)
        message(FATAL_ERROR "GUI/Batch module identity mismatch: ${field}")
    endif()
endforeach()
string(JSON guiFps GET "${GUI}" fps)
string(JSON jobFps GET "${JOB}" frames fps)
if(NOT guiFps STREQUAL jobFps)
    message(FATAL_ERROR "GUI/Job FPS mismatch")
endif()
file(WRITE "${OUTPUT_DIR}/latest.txt" "${RUN}\n")
message(STATUS "M1-A saved Scene / Job parameters, FPS, Seed, API, Build ID and image parity passed: ${RUN}")
