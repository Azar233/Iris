if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "BufferPreviewCapture needs RENDERER, SOURCE_DIR and OUTPUT_DIR")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
function(capture name window_width window_height path ssao tab)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        --unset=MYRENDERER_BUFFER_EXPORT --unset=MYRENDERER_BUFFERS_COLLAPSED
        --unset=MYRENDERER_RENDER_WIDTH --unset=MYRENDERER_RENDER_HEIGHT
        MYRENDERER_SMOKE_TEST=1 MYRENDERER_RENDER_PATH=${path} MYRENDERER_SSAO=${ssao}
        MYRENDERER_TAA=0 MYRENDERER_MSAA=4 MYRENDERER_GRID=0 MYRENDERER_AXES=0
        MYRENDERER_PBR=1 MYRENDERER_IBL=1 MYRENDERER_SHADOWS=1 MYRENDERER_GROUND=1
        MYRENDERER_HIDE_SELECTION_OUTLINE=1
        MYRENDERER_EDITOR_WINDOW_WIDTH=${window_width} MYRENDERER_EDITOR_WINDOW_HEIGHT=${window_height}
        MYRENDERER_EDITOR_SCREENSHOT_TAB=${tab} MYRENDERER_EDITOR_SCREENSHOT_WARMUP=2
        MYRENDERER_EDITOR_SCREENSHOT=${OUTPUT_DIR}/${name}-editor.png
        MYRENDERER_SCREENSHOT=${OUTPUT_DIR}/${name}-final.png
        ${ARGN}
        "${RENDERER}" "${SOURCE_DIR}/assets/models/pbr_material_test.gltf"
        WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT EXISTS "${OUTPUT_DIR}/${name}-editor.png" OR NOT EXISTS "${OUTPUT_DIR}/${name}-final.png")
        message(FATAL_ERROR "Buffer preview capture failed: ${name}")
    endif()
endfunction()
capture(closed 1440 900 1 1 object)
capture(open 1440 900 1 1 buffers MYRENDERER_BUFFER_EXPORT=${OUTPUT_DIR}/channels)
capture(minimum 1100 680 1 1 buffers )
capture(collapsed 1440 900 1 1 buffers MYRENDERER_BUFFERS_COLLAPSED=1)
capture(forward 1100 680 0 1 buffers )
capture(ssao-disabled 1440 900 1 0 buffers MYRENDERER_BUFFER_EXPORT=${OUTPUT_DIR}/ssao-disabled-channels)
foreach(channel albedo normal material depth motion ssao)
    if(NOT EXISTS "${OUTPUT_DIR}/channels/${channel}.png")
        message(FATAL_ERROR "Missing exported GPU channel: ${channel}")
    endif()
endforeach()
if(EXISTS "${OUTPUT_DIR}/ssao-disabled-channels/ssao.png")
    message(FATAL_ERROR "Disabled SSAO exported a stale buffer")
endif()
# Reject accidentally clamped startup sizes (e.g. an explicit zero becomes 64).
file(READ "${OUTPUT_DIR}/channels/normal.png" dimensions OFFSET 16 LIMIT 8 HEX)
string(SUBSTRING "${dimensions}" 0 8 width_hex)
string(SUBSTRING "${dimensions}" 8 8 height_hex)
math(EXPR width "0x${width_hex}")
math(EXPR height "0x${height_hex}")
if(width LESS 200 OR height LESS 120)
    message(FATAL_ERROR "Unexpectedly small channel export: ${width} x ${height}")
endif()
file(SHA256 "${OUTPUT_DIR}/closed-final.png" closed_hash)
file(SHA256 "${OUTPUT_DIR}/open-final.png" open_hash)
if(NOT closed_hash STREQUAL open_hash)
    message(FATAL_ERROR "Opening buffer inspection changed final scene pixels")
endif()
message(STATUS "Buffer inspection captures PASS: 6 channels, disabled SSAO absent, final SHA256 unchanged")
