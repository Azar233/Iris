if(NOT DEFINED RENDERER OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "StylizedBenchmark requires RENDERER, SOURCE_DIR, and OUTPUT_DIR")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(run_stylized_benchmark name scene preset render_path msaa width height taa)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "MYRENDERER_BENCHMARK_WARMUP=30"
            "MYRENDERER_BENCHMARK_FRAMES=90"
            "MYRENDERER_RENDER_WIDTH=${width}"
            "MYRENDERER_RENDER_HEIGHT=${height}"
            "MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${name}.json"
            "MYRENDERER_STYLIZED=1"
            "MYRENDERER_STYLIZED_PRESET=${preset}"
            "MYRENDERER_RENDER_PATH=${render_path}"
            "MYRENDERER_MSAA=${msaa}"
            "MYRENDERER_TAA=${taa}"
            "MYRENDERER_HIDE_SELECTION_OUTLINE=1"
            "${RENDERER}"
            "${SOURCE_DIR}/assets/scenes/fixtures/${scene}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE benchmark_result
    )
    if(NOT benchmark_result EQUAL 0)
        message(FATAL_ERROR "Stylized benchmark failed for ${name}")
    endif()
endfunction()

# Low is the inexpensive publishing tier. High enables the complete composite
# at full HD and keeps all measurements in the build directory.
run_stylized_benchmark(
    sr_p2c_low
    15_stylized_clean_toon_gallery.myscene
    1 0 1 1280 720 0
)
run_stylized_benchmark(
    sr_p2c_high
    17_stylized_night_aurora_outdoor.myscene
    3 1 4 1920 1080 1
)
