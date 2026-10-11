if(NOT IS_DIRECTORY "${SOURCE_DIR}/assets" OR NOT DEFINED DESTINATION_DIR)
    message(FATAL_ERROR "Source assets and destination are required")
endif()
# COPY preserves timestamps and skips unchanged files. Do not rewrite every
# texture after a code-only relink, or launch hundreds of copy subprocesses.
file(COPY "${SOURCE_DIR}/assets" "${SOURCE_DIR}/shaders" DESTINATION "${DESTINATION_DIR}")
