"""Measure Iris P0b transitions and full target-SPP latency, serially."""
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import subprocess
import time

from MeasureSponzaPerformance import ROOT, SCENES, digest, scene_inputs, telemetry


ALTERNATE = "assets/benchmarks/m3c/stanford-opaque.myscene"
CASES = {
    "intel-gl-transitions": ("intel", "raster", "transitions"),
    "intel-vk-rq-transitions": ("intel", "vulkan-raster", "transitions"),
    "intel-pt-transitions": ("intel", "gpu-path-traced", "transitions"),
    "intel-gl-native": ("intel", "raster", "native"),
    "classic-gl-native": ("classic", "raster", "native"),
    "intel-pt-full64": ("intel", "gpu-path-traced", "full64"),
}
PHASES = ["cold", "warm", "camera-move", "camera-move", "camera-restore", "warm",
          "resize-down", "warm", "resize-restore", "warm", "rigid-move", "warm",
          "rigid-restore", "warm", "scene-away", "warm", "scene-return", "warm"]
CHANGES = {0, 2, 3, 4, 6, 8, 10, 12, 14, 16}


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def power_state():
    class Status(ctypes.Structure):
        _fields_ = [("ac", ctypes.c_ubyte), ("battery", ctypes.c_ubyte),
                    ("percent", ctypes.c_ubyte), ("reserved", ctypes.c_ubyte),
                    ("lifeSeconds", ctypes.c_ulong), ("fullSeconds", ctypes.c_ulong)]
    status = Status()
    ok = ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(status))
    plan = subprocess.run(["powercfg", "/GETACTIVESCHEME"], capture_output=True,
                          timeout=10).stdout.decode(errors="replace").strip()
    return {"ac": status.ac if ok else None, "batteryPercent": status.percent if ok else None,
            "powerPlan": plan, "gpu": telemetry(), "clocksLocked": False}


def clean_environment():
    return {k: v for k, v in os.environ.items() if not k.startswith(("IRIS_", "MYRENDERER_"))
            and k not in ("VK_INSTANCE_LAYERS", "VK_LAYER_PATH", "VK_LAYER_SETTINGS_PATH")}


def validate(report, case, strict=False):
    scene, backend, mode = CASES[case]
    assert report["benchmarkProtocolVersion"] == 2 and report["frameTraceVersion"] == 1
    assert report["backend"] == backend and not report["vsync"]
    assert report["validationEnabled"] == (strict and backend != "raster")
    assert report["cpuFrameP50Ms"] > 0 and math.isfinite(report["cpuFrameP95Ms"])
    if mode == "native":
        assert report["msaaSamples"] == 4
        assert report["warmupFrames"] == 60 and report["measurementFrames"] == 180
        assert report["width"] >= 1100 and report["height"] >= 600
        return
    trace = report["frameTrace"]
    assert len(trace) == (18 if mode == "transitions" else 64)
    assert [f["frame"] for f in trace] == list(range(len(trace)))
    assert all(f["cpuMs"] > 0 for f in trace)
    if mode == "transitions":
        assert [f["phase"] for f in trace] == PHASES
        for i, f in enumerate(trace):
            assert (f["width"], f["height"]) == ((800, 450) if i in (6, 7) else (1024, 576))
            assert Path(f["scene"]) == (ROOT / (ALTERNATE if i in (14, 15) else SCENES[scene])).resolve()
        yaw, x = trace[0]["yaw"], trace[0]["entityX"]
        assert abs(trace[2]["yaw"] - yaw - 2) < 0.001
        assert abs(trace[3]["yaw"] - yaw + 2) < 0.001
        assert abs(trace[4]["yaw"] - yaw) < 0.001
        assert abs(trace[10]["entityX"] - x - 0.2) < 0.001
        assert abs(trace[12]["entityX"] - x) < 0.001
    if backend != "raster":
        assert all(f["gpuTimingSynchronous"] and f["gpuTimingValid"] for f in trace)
        assert all(f["validationErrors"] == 0 for f in trace)
        assert all(f["readbackMemoryFlags"] & 6 == 6 for f in trace)
    if backend == "vulkan-raster":
        for i, f in enumerate(trace):
            assert f["dispatchBlasBuilds"] > 0 if i in (0, 14, 16) else f["dispatchBlasBuilds"] == 0
            assert f["dispatchTlasBuilds"] == (1 if i in (0, 14, 16) else 0)
            assert f["dispatchTlasUpdates"] == (1 if i in (10, 12) else 0)
    if backend == "gpu-path-traced":
        assert all(f["dispatchBlasBuilds"] == f["dispatchTlasBuilds"] == f["dispatchTlasUpdates"] == 0 for f in trace)
        if mode == "transitions":
            generation = spp = 0
            for i, f in enumerate(trace):
                if i in CHANGES:
                    generation += 1
                    spp = 0
                spp += 1
                assert f["backendGeneration"] == generation and f["spp"] == spp
                assert f["backendInitialized"] == (i in CHANGES)
                assert f["initialBlasBuilds"] > 0 if i in CHANGES else f["initialBlasBuilds"] == 0
        else:
            assert [f["spp"] for f in trace] == list(range(1, 65))
            assert all(f["backendGeneration"] == 1 for f in trace)
            assert report["ptCompletedSpp"] == 64 and report["ptTargetReachedGenerationMs"] > 0
            assert report["ptTargetReachedFromRunMs"] >= report["ptTargetReachedGenerationMs"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default="build-ci-msvc/Release/Iris.exe")
    parser.add_argument("--output", default=".local/benchmarks/m3p0b")
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--strict", action="store_true", help="Separate correctness run, not comparable timing")
    parser.add_argument("--layer-directory", type=Path, help="Validation-layer manifest directory for --strict")
    args = parser.parse_args()
    if args.layer_directory and not args.strict:
        parser.error("--layer-directory requires --strict")
    layer = args.layer_directory.resolve() if args.layer_directory else None
    if layer and not (layer / "VkLayer_khronos_validation.json").is_file():
        parser.error("Validation-layer manifest missing")
    exe, output = (ROOT / args.exe).resolve(), (ROOT / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    scenes = {SCENES[CASES[c][0]] for c in args.cases} | {ALTERNATE}
    protocol = {"version": 1, "scope": "P0b; instrumentation only, no optimization", "cases": args.cases,
                "repeats": 2, "exeSha256": digest(exe), "runnerSha256": digest(Path(__file__)),
                "helperSha256": digest(ROOT / "tools/MeasureSponzaPerformance.py"),
                "workspace": [1920, 1080], "fixedInternal": [1024, 576], "smallInternal": [800, 450],
                "nativeFrames": [60, 180], "ptFrames": [4, 60], "ptSpp": 64,
                "phases": PHASES, "validation": args.strict, "vsync": False,
                "inputs": {s: scene_inputs(s) for s in sorted(scenes)}}
    if layer:
        protocol["validationLayer"] = {"path": str(layer), "manifestSha256": digest(layer / "VkLayer_khronos_validation.json")}
    path = output / "protocol.json"
    if path.exists():
        assert args.resume and json.loads(path.read_text(encoding="utf-8")) == protocol, "Protocol changed; retain evidence and choose another directory"
    else:
        write(path, protocol)
    results = []
    for case in args.cases:
        scene, backend, mode = CASES[case]
        for repeat in (1, 2):
            assert digest(exe) == protocol["exeSha256"], "Executable changed"
            name = f"{case}-{repeat}"
            report_path, meta = output / f"{name}.json", output / f"{name}-run.json"
            if not (args.resume and report_path.exists() and meta.exists()):
                env = clean_environment()
                env.update({"MYRENDERER_BENCHMARK_WARMUP": "60" if mode == "native" else "4",
                            "MYRENDERER_BENCHMARK_FRAMES": "180" if mode == "native" else "60",
                            "MYRENDERER_BENCHMARK_OUTPUT": str(report_path),
                            "MYRENDERER_EDITOR_WINDOW_WIDTH": "1920", "MYRENDERER_EDITOR_WINDOW_HEIGHT": "1080",
                            "MYRENDERER_MSAA": "4" if mode == "native" else "1", "MYRENDERER_ANIMATION_TIME": "0",
                            "IRIS_STATE_DIRECTORY": str(output / f"{name}-state"),
                            "IRIS_VULKAN_DISABLE_VSYNC": "1", "IRIS_BENCHMARK_SHADOWS": "1" if backend == "raster" else "0",
                            "IRIS_BENCHMARK_RAY_QUERY": "1" if backend == "vulkan-raster" else "0",
                            "IRIS_BENCHMARK_PT_SPP": "64"})
                if args.strict:
                    env.update({"IRIS_VULKAN_REQUIRE_VALIDATION": "1", "IRIS_VULKAN_SYNCHRONIZATION_VALIDATION": "1"})
                    if layer:
                        env["VK_LAYER_PATH"] = str(layer)
                else:
                    env["IRIS_VULKAN_DISABLE_VALIDATION"] = "1"
                if mode == "native":
                    env["IRIS_BENCHMARK_NATIVE_VIEWPORT"] = "1"
                else:
                    env.update({"MYRENDERER_RENDER_WIDTH": "1024", "MYRENDERER_RENDER_HEIGHT": "576",
                                "IRIS_BENCHMARK_FRAME_TRACE": "1"})
                if mode == "transitions":
                    env.update({"IRIS_BENCHMARK_TRANSITIONS": "1", "IRIS_BENCHMARK_ALTERNATE_SCENE": str(ROOT / ALTERNATE)})
                if backend != "raster":
                    env["IRIS_VULKAN_PATH_TRACER" if backend == "gpu-path-traced" else "IRIS_VULKAN_RASTER"] = "1"
                before, started = power_state(), time.monotonic()
                print(f"Running {name}", flush=True)
                with (output / f"{name}.log").open("w", encoding="utf-8") as log:
                    result = subprocess.run([str(exe), str(ROOT / SCENES[scene])], cwd=ROOT,
                                            env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1200)
                write(meta, {"exitCode": result.returncode, "processSeconds": time.monotonic() - started,
                             "before": before, "after": power_state()})
                assert result.returncode == 0, f"{name}: exit {result.returncode}; retain log"
            report = json.loads(report_path.read_text(encoding="utf-8"))
            validate(report, case, args.strict)
            results.append({"case": case, "repeat": repeat, "cpuP50Ms": report["cpuFrameP50Ms"],
                            "cpuP95Ms": report["cpuFrameP95Ms"], "size": [report["width"], report["height"]],
                            "targetGenerationMs": report["ptTargetReachedGenerationMs"]})
            print(f"  verified {name}: CPU P50 {report['cpuFrameP50Ms']:.2f} ms", flush=True)
    write(output / "summary.json", results)


if __name__ == "__main__":
    main()
