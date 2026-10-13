if(NOT DEFINED IRIS_EXECUTABLE OR NOT DEFINED IRIS_ROOT)
    message(FATAL_ERROR "Iris executable and repository root are required")
endif()
set(evidence "${IRIS_ROOT}/.local/verification/a3m1/target-gpu")
file(MAKE_DIRECTORY "${evidence}")
# Expected rejection diagnostics include driver 'error C0000' lines. Keep them
# out of MSBuild's diagnostic parser while preserving the complete evidence.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    MYRENDERER_SMOKE_TEST=1
    "IRIS_STATE_DIRECTORY=${IRIS_ROOT}/.local/verification/a3m1/target-state"
    "IRIS_SHADER_MATERIAL_ACCEPTANCE_DIRECTORY=${evidence}"
    "${IRIS_EXECUTABLE}" "${IRIS_ROOT}/assets/scenes/fixtures/29_shader_material.myscene"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
file(WRITE "${evidence}/acceptance.log" "${output}\n${errors}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "ShaderMaterial GPU acceptance failed (${result}); see ${evidence}/acceptance.log")
endif()
message(STATUS "ShaderMaterial GPU acceptance PASS; evidence ${evidence}")
