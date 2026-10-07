cmake_minimum_required(VERSION 3.20)
foreach(required RENDERER COMPARATOR CONTRACTS SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT_DIR}/acceptance.txt" "RUNNING: M2-C gates pending\n")
execute_process(COMMAND "${CONTRACTS}" "${OUTPUT_DIR}" RESULT_VARIABLE contracts)
if(NOT contracts EQUAL 0)
    message(FATAL_ERROR "Water GPU contracts failed")
endif()
# Retain the M2-A thresholds and its exact time/quality/sample contracts.
include("${SOURCE_DIR}/tools/M2AHeroAcceptance.cmake")
file(WRITE "${OUTPUT_DIR}/acceptance.txt" "RUNNING: Hero budgets passed; M2-C integration gates pending\n")
set(hero "${SOURCE_DIR}/assets/scenes/fixtures/28_native_ocean_clouds.myscene")
function(water_capture name scene path samples taa optics reflection)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        MYRENDERER_BENCHMARK_FRAMES=60 MYRENDERER_BENCHMARK_WARMUP=16
        "MYRENDERER_BENCHMARK_OUTPUT=${OUTPUT_DIR}/${name}.json"
        "MYRENDERER_SCREENSHOT=${OUTPUT_DIR}/${name}.png" MYRENDERER_SCREENSHOT_WARMUP=16
        MYRENDERER_RENDER_WIDTH=1280 MYRENDERER_RENDER_HEIGHT=720
        "MYRENDERER_RENDER_PATH=${path}" "MYRENDERER_MSAA=${samples}" "MYRENDERER_TAA=${taa}"
        "MYRENDERER_WATER_SURFACE_OPTICS=${optics}" "MYRENDERER_WATER_CLOUD_REFLECTION=${reflection}"
        MYRENDERER_TIMELINE_FRAME=0 MYRENDERER_ANIMATION_TIME=0 MYRENDERER_ANIMATION_FRAME_STEP=0
        MYRENDERER_CAMERA_HEIGHT_DEMO_STEP=0 MYRENDERER_HIDE_SELECTION_OUTLINE=1
        "MYRENDERER_RENDER_QUEUE_STATE=${OUTPUT_DIR}/queue.json" ${ARGN}
        "${RENDERER}" "${scene}" WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 180)
    file(WRITE "${OUTPUT_DIR}/${name}.log" "${out}\n${err}")
    if(NOT result EQUAL 0 OR NOT EXISTS "${OUTPUT_DIR}/${name}.png")
        message(FATAL_ERROR "Water capture failed: ${name}/${result}")
    endif()
    file(READ "${OUTPUT_DIR}/${name}.json" report)
    string(JSON mode GET "${report}" waterSurfaceOptics)
    string(JSON gain GET "${report}" waterCloudReflectionStrength)
    string(JSON actual_samples GET "${report}" msaaSamples)
    string(JSON count GET "${report}" gpuFrameMeasurements)
    if((optics AND NOT mode) OR (NOT optics AND mode)
        OR NOT gain EQUAL reflection OR NOT actual_samples EQUAL samples OR count LESS 30)
        message(FATAL_ERROR "Capture input mismatch: ${name}")
    endif()
endfunction()
function(water_compare first second mae changed expected)
    execute_process(COMMAND "${COMPARATOR}" "${OUTPUT_DIR}/${first}.png" "${OUTPUT_DIR}/${second}.png"
        ${mae} ${changed} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    file(WRITE "${OUTPUT_DIR}/${first}-${second}.txt" "${out}\n${err}")
    if(NOT result EQUAL expected)
        message(FATAL_ERROR "Water comparison failed: ${first}/${second}/${result}: ${out}")
    endif()
endfunction()
foreach(samples 1 4)
    foreach(taa 0 1)
        water_capture(forward-msaa${samples}-taa${taa} "${hero}" 0 ${samples} ${taa} 1 1)
        water_capture(deferred-msaa${samples}-taa${taa} "${hero}" 1 ${samples} ${taa} 1 1)
        water_compare(forward-msaa${samples}-taa${taa} deferred-msaa${samples}-taa${taa} 0.004 0.02 0)
    endforeach()
endforeach()
water_capture(reflection-off "${hero}" 1 4 1 1 0)
water_compare(reflection-off deferred-msaa4-taa1 0.00001 0.00001 1)
water_capture(legacy-optics "${hero}" 1 4 1 0 0)
water_compare(legacy-optics deferred-msaa4-taa1 0.001 0.01 1)
set(underwater "${SOURCE_DIR}/assets/scenes/fixtures/22_ocean_underwater.myscene")
water_capture(underwater-legacy "${underwater}" 1 4 0 0 0)
water_capture(underwater-filtered "${underwater}" 1 4 0 1 0)
water_compare(underwater-legacy underwater-filtered 0.001 0.01 1)
water_capture(depth-filtered "${SOURCE_DIR}/assets/scenes/fixtures/21_ocean_depth.myscene" 1 4 0 1 0)
# Look upward through an interface closer than the near clip plane. There is no
# front-depth sample: legacy fog incorrectly integrates 24 metres of clear sky.
set(near_scene "{\"format\":\"MyRendererScene\",\"version\":1,\"camera\":{\"target\":[0,5.1761524,-18],\"yawDegrees\":0,\"pitchDegrees\":-60,\"distance\":6,\"fieldOfViewDegrees\":58},\"renderer\":{\"showGrid\":false,\"showAxes\":false,\"waterEnabled\":true,\"waterLevel\":0,\"waterAmplitude\":0,\"waterExtent\":110,\"atmosphereEnabled\":true,\"sunElevationDegrees\":12,\"sunAzimuthDegrees\":118,\"skyIntensity\":3,\"sunIntensity\":3,\"skyTurbidity\":1.2,\"aerialPerspectiveEnabled\":false,\"toneMapping\":true,\"bloom\":false,\"exposure\":1},\"entities\":[]}")
file(WRITE "${OUTPUT_DIR}/near-below.myscene" "${near_scene}\n")
string(REPLACE "5.1761524" "5.2161524" near_above "${near_scene}")
file(WRITE "${OUTPUT_DIR}/near-above.myscene" "${near_above}\n")
water_capture(near-below-legacy "${OUTPUT_DIR}/near-below.myscene" 1 4 0 0 0)
water_capture(near-below-filtered "${OUTPUT_DIR}/near-below.myscene" 1 4 0 1 0)
water_capture(near-above-filtered "${OUTPUT_DIR}/near-above.myscene" 1 4 0 1 0)
water_compare(near-below-legacy near-below-filtered 0.01 0.05 1)
water_compare(near-above-filtered near-below-filtered 0.015 1.0 0)
water_capture(crossing "${underwater}" 1 4 1 1 0 MYRENDERER_CAMERA_HEIGHT_DEMO_STEP=0.1)
file(READ "${OUTPUT_DIR}/crossing.json" crossing)
string(JSON transitions GET "${crossing}" waterMediumTransitions)
string(JSON underwater GET "${crossing}" cameraUnderWater)
if(transitions LESS 1 OR underwater)
    message(FATAL_ERROR "Camera must cross the animated surface and reset history")
endif()
execute_process(COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File "${SOURCE_DIR}/tools/M2CWaterImageMetrics.ps1" -OutputDirectory "${OUTPUT_DIR}"
    RESULT_VARIABLE metrics)
if(NOT metrics EQUAL 0)
    message(FATAL_ERROR "Water image semantic checks failed")
endif()
include("${SOURCE_DIR}/tools/M2CCloudWaterIntegration.cmake")
file(WRITE "${OUTPUT_DIR}/acceptance.txt"
    "PASS: GPU mesh/medium contracts, fixed budgets, repeat, 8 path/MSAA/TAA combinations, reflection contribution, interface crossing and image metrics\n")
