if(NOT DEFINED RENDERER OR NOT DEFINED BATCH OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "LightEntityAcceptance requires RENDERER, BATCH, SOURCE_DIR, OUTPUT_DIR")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env MYRENDERER_SMOKE_TEST=1
    MYRENDERER_LIGHT_ENTITY_TEST=${OUTPUT_DIR}
    MYRENDERER_EDITOR_SCREENSHOT=${OUTPUT_DIR}/editor-1440.png
    MYRENDERER_EDITOR_WINDOW_WIDTH=1440 MYRENDERER_EDITOR_WINDOW_HEIGHT=900
    "${RENDERER}" "${SOURCE_DIR}/assets/models/cube.obj"
    WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Light entity commands/persistence failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env MYRENDERER_SMOKE_TEST=1
    MYRENDERER_LIGHT_UI_INTERACTION=1
    "${RENDERER}" "${SOURCE_DIR}/assets/models/cube.obj"
    WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Actual Add Light mouse interaction failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env MYRENDERER_SMOKE_TEST=1
    MYRENDERER_LIGHT_ENTITY_TEST=${OUTPUT_DIR}
    MYRENDERER_EDITOR_SCREENSHOT=${OUTPUT_DIR}/editor-1100.png
    MYRENDERER_EDITOR_WINDOW_WIDTH=1100 MYRENDERER_EDITOR_WINDOW_HEIGHT=680
    "${RENDERER}" "${SOURCE_DIR}/assets/models/cube.obj"
    WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Minimum workspace light capture failed")
endif()
foreach(path 0 1)
    foreach(scene entities legacy disabled)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env MYRENDERER_SMOKE_TEST=1
            MYRENDERER_RENDER_PATH=${path} MYRENDERER_HIDE_SELECTION_OUTLINE=1
            MYRENDERER_RENDER_WIDTH=480 MYRENDERER_RENDER_HEIGHT=320 MYRENDERER_TAA=0
            MYRENDERER_SCREENSHOT=${OUTPUT_DIR}/${scene}-${path}.png
            "${RENDERER}" "${OUTPUT_DIR}/${scene}.myscene"
            WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result)
        if(NOT result EQUAL 0 OR NOT EXISTS "${OUTPUT_DIR}/${scene}-${path}.png")
            message(FATAL_ERROR "Raster light capture failed: ${scene}/${path}")
        endif()
    endforeach()
    file(SHA256 "${OUTPUT_DIR}/entities-${path}.png" entity_hash)
    file(SHA256 "${OUTPUT_DIR}/legacy-${path}.png" legacy_hash)
    file(SHA256 "${OUTPUT_DIR}/disabled-${path}.png" disabled_hash)
    if(NOT entity_hash STREQUAL legacy_hash OR entity_hash STREQUAL disabled_hash)
        message(FATAL_ERROR "Entity lighting parity / active contribution failed on raster path ${path}")
    endif()
endforeach()
string(TIMESTAMP run_id "%Y%m%d-%H%M%S")
set(CPU_DIR "${OUTPUT_DIR}/cpu-${run_id}")
file(MAKE_DIRECTORY "${CPU_DIR}")
foreach(scene entities legacy disabled)
    file(WRITE "${OUTPUT_DIR}/${scene}.renderjob" "{\"format\":\"MyRendererRenderJob\",\"schemaVersion\":1,\"scene\":\"${scene}.myscene\",\"renderer\":\"cpu-path-traced\",\"camera\":\"scene\",\"resolution\":[96,64],\"frames\":{\"start\":0,\"end\":0,\"fps\":24},\"sampling\":{\"spp\":4,\"maxDepth\":2,\"seed\":7},\"aovs\":[\"beauty\"],\"output\":{\"path\":\"${CPU_DIR}/${scene}\",\"formats\":[\"png\"],\"resume\":false},\"simulationCache\":\"\",\"failurePolicy\":\"stop\"}")
    execute_process(COMMAND "${BATCH}" render-frame "${OUTPUT_DIR}/${scene}.renderjob" 0
        WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT EXISTS "${CPU_DIR}/${scene}.png")
        message(FATAL_ERROR "CPU entity light render failed: ${scene}")
    endif()
endforeach()
file(SHA256 "${CPU_DIR}/entities.png" entity_hash)
file(SHA256 "${CPU_DIR}/legacy.png" legacy_hash)
file(SHA256 "${CPU_DIR}/disabled.png" disabled_hash)
if(NOT entity_hash STREQUAL legacy_hash OR entity_hash STREQUAL disabled_hash)
    message(FATAL_ERROR "CPU entity lighting parity / active contribution failed")
endif()
message(STATUS "Light entities PASS: commands, real UI, persistence, both raster paths and CPU legacy parity / on-off")
