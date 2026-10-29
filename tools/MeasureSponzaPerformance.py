"""Run the existing Iris benchmark serially, with a frozen static protocol."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import time
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]
SCENES = {
    "intel": "assets/benchmarks/m3c/sponza-atrium.myscene",
    "classic": "assets/benchmarks/khronos-sponza/sponza.myscene",
    "glass": "assets/benchmarks/m3c/stanford-glass.myscene",
}
# scene, backend, GL shadows, ray query, PT total target SPP
CASES = {
    "intel-gl-csm": ("intel", "raster", 1, 0, 64),
    "intel-gl-unshadowed": ("intel", "raster", 0, 0, 64),
    "intel-vk-unshadowed": ("intel", "vulkan-raster", 0, 0, 64),
    "intel-vk-rq": ("intel", "vulkan-raster", 0, 1, 64),
    "intel-pt": ("intel", "gpu-path-traced", 0, 0, 64),
    "intel-pt-completed": ("intel", "gpu-path-traced", 0, 0, 4),
    "classic-gl-csm": ("classic", "raster", 1, 0, 64),
    "classic-vk-diagnostic": ("classic", "vulkan-raster", 0, 0, 64),
    "glass-pt": ("glass", "gpu-path-traced", 0, 0, 64),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def telemetry():
    try:
        result = subprocess.run(
            ["nvidia-smi", "--query-gpu=name,driver_version,pstate,power.limit,temperature.gpu,clocks.gr,utilization.gpu",
             "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=10)
        return result.stdout.strip() if result.returncode == 0 else "unavailable"
    except (OSError, subprocess.TimeoutExpired):
        return "unavailable"


def scene_inputs(scene):
    scene_path = ROOT / scene
    files = {scene_path}
    for entity in json.loads(scene_path.read_text(encoding="utf-8"))["entities"]:
        if not entity.get("model") or entity["model"].startswith("builtin:"):
            continue
        model = (scene_path.parent / entity["model"]).resolve()
        files.add(model)
        gltf = json.loads(model.read_text(encoding="utf-8"))
        for key in ("buffers", "images"):
            for item in gltf.get(key, []):
                uri = item.get("uri", "")
                if uri and not uri.startswith("data:"):
                    files.add((model.parent / unquote(uri)).resolve())
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(files)}


def validate(report, case):
    _, backend, shadows, ray_query, spp = CASES[case]
    assert report["benchmarkProtocolVersion"] == 2
    assert report["backend"] == backend and not report["vsync"]
    assert not report["validationEnabled"] and not report["synchronizationValidation"]
    assert (report["width"], report["height"]) == (1024, 576)
    assert report["measurementFrames"] == report["cpuFrameMeasurements"] == 30
    assert report["warmupFrames"] == 4 and report["msaaSamples"] == 1
    assert report["shadowsEnabled"] == bool(shadows)
    assert report["rayQueryShadows"] == bool(ray_query)
    assert report["cpuFrameP50Ms"] > 0 and math.isfinite(report["cpuFrameP95Ms"])
    if backend == "vulkan-raster":
        assert report["vulkanPresentMode"] == "immediate"
        assert report["vulkanCounters"]["readbackBytes"]["p50"] == 1024 * 576 * 4
    if backend == "gpu-path-traced":
        assert report["ptTargetSpp"] == spp and report["ptDepth"] == 8
        assert report["ptSeed"] == 20261026
        if spp == 4:
            # Reproduces the stale warmup timestamp bug if it is reintroduced.
            assert report["ptCompletedSpp"] == 4
            assert report["gpuFrameMeasurements"] == report["activeCpuFrameMeasurements"] == 0
            assert not report["gpuTimingValid"] and report["completedCpuFrameMeasurements"] == 30
        else:
            assert report["ptCompletedSpp"] == 34
            assert report["gpuFrameMeasurements"] == report["activeCpuFrameMeasurements"] == 30
            assert report["completedCpuFrameMeasurements"] == 0 and report["activeSppPerSecond"] > 0
            assert report["vulkanCounters"]["readbackBytes"]["p50"] == 1024 * 576 * 128
    if not case.endswith("completed"):
        assert report["gpuTimingValid"] and report["gpuFrameMeasurements"] > 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default="build-ci-msvc/Release/Iris.exe")
    parser.add_argument("--output", default=".local/benchmarks/m3p0a")
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    parser.add_argument("--resume", action="store_true", help="Reuse only validated reports from the identical protocol/binary")
    args = parser.parse_args()
    exe = (ROOT / args.exe).resolve()
    output = (ROOT / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    protocol = {
        "scope": "M3-P0a static; motion/resize/scene-switch and target-SPP latency remain separate",
        "exeSha256": digest(exe), "runnerSha256": digest(Path(__file__)), "cases": args.cases, "repeats": 2,
        "warmupFrames": 4, "measurementFrames": 30, "internalSize": [1024, 576],
        "workspaceSize": [1920, 1080], "msaa": 1, "validation": False,
        "vsync": False, "ptDepth": 8, "ptSeed": 20261026,
        "scenes": {key: {"path": SCENES[key], "inputs": scene_inputs(SCENES[key])}
                   for key in sorted({CASES[c][0] for c in args.cases})},
        "qualityNotes": "GL CSM, Vulkan unshadowed and Ray Query are separate algorithms, not equal-quality speedups; classic Vulkan image is known to fail.",
        "cpuPassNotes": "Vulkan.Pass.* are nested in Vulkan.ActivePass; GPU execution overlaps CPU submit/wait. Do not sum these intervals.",
    }
    protocol_path = output / "protocol.json"
    if protocol_path.exists():
        if not args.resume or json.loads(protocol_path.read_text(encoding="utf-8")) != protocol:
            raise RuntimeError("Existing protocol differs or --resume missing; choose a new output directory")
    else:
        protocol_path.write_text(json.dumps(protocol, indent=2) + "\n", encoding="utf-8")
    results = []
    for case in args.cases:
        scene, backend, shadows, ray_query, spp = CASES[case]
        for repeat in (1, 2):
            if digest(exe) != protocol["exeSha256"]:
                raise RuntimeError("Benchmark executable changed during this protocol")
            name = f"{case}-{repeat}"
            report_path = output / f"{name}.json"
            metadata_path = output / f"{name}-run.json"
            if args.resume and report_path.exists() and metadata_path.exists():
                report = json.loads(report_path.read_text(encoding="utf-8"))
                validate(report, case)
            else:
                env = {k: v for k, v in os.environ.items()
                       if not k.startswith(("IRIS_", "MYRENDERER_"))
                       and k not in ("VK_INSTANCE_LAYERS", "VK_LAYER_PATH", "VK_LAYER_SETTINGS_PATH")}
                env.update({
                    "MYRENDERER_BENCHMARK_WARMUP": "4", "MYRENDERER_BENCHMARK_FRAMES": "30",
                    "MYRENDERER_BENCHMARK_OUTPUT": str(report_path),
                    "MYRENDERER_RENDER_WIDTH": "1024", "MYRENDERER_RENDER_HEIGHT": "576",
                    "MYRENDERER_EDITOR_WINDOW_WIDTH": "1920", "MYRENDERER_EDITOR_WINDOW_HEIGHT": "1080",
                    "MYRENDERER_MSAA": "1", "MYRENDERER_ANIMATION_TIME": "0",
                    "IRIS_STATE_DIRECTORY": str(output / f"{name}-state"),
                    "IRIS_VULKAN_DISABLE_VALIDATION": "1", "IRIS_VULKAN_DISABLE_VSYNC": "1",
                    "IRIS_BENCHMARK_SHADOWS": str(shadows), "IRIS_BENCHMARK_RAY_QUERY": str(ray_query),
                    "IRIS_BENCHMARK_PT_SPP": str(spp),
                })
                if backend != "raster":
                    env["IRIS_VULKAN_PATH_TRACER" if backend == "gpu-path-traced" else "IRIS_VULKAN_RASTER"] = "1"
                before = telemetry()
                started = time.monotonic()
                print(f"Running {name}", flush=True)
                with (output / f"{name}.log").open("w", encoding="utf-8") as log:
                    result = subprocess.run([str(exe), str(ROOT / SCENES[scene])], cwd=ROOT,
                                            env=env, stdout=log, stderr=subprocess.STDOUT, timeout=900)
                if result.returncode:
                    raise RuntimeError(f"{name} failed: exit {result.returncode}; retain log")
                report = json.loads(report_path.read_text(encoding="utf-8"))
                validate(report, case)
                if report_path.stat().st_mtime < time.time() - (time.monotonic()-started) - 2:
                    raise RuntimeError(f"{name} reused an old report")
                metadata_path.write_text(json.dumps({"processSeconds": time.monotonic()-started,
                    "gpuBefore": before, "gpuAfter": telemetry(), "exitCode": result.returncode}, indent=2)+"\n", encoding="utf-8")
            results.append({"case": case, "repeat": repeat, "report": report_path.name,
                            "cpuP50Ms": report["cpuFrameP50Ms"], "cpuP95Ms": report["cpuFrameP95Ms"],
                            "gpuP50Ms": report["gpuFrameP50Ms"] if report["gpuTimingValid"] else None})
            print(f"  verified {name}: CPU P50={report['cpuFrameP50Ms']:.2f} ms", flush=True)
    (output / "summary.json").write_text(json.dumps(results, indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
