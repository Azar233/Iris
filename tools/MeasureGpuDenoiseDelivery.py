"""Native 720p D4 wall-frame evidence; runs one Iris process at a time."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import threading
import time
from MeasureSponzaPerformance import scene_inputs

ROOT = Path(__file__).resolve().parents[1]
SCENES = ['sponza-atrium', 'stanford-glass', 'stanford-opaque']


def clean_environment():
    return {k: v for k, v in os.environ.items()
            if not k.startswith(('IRIS_', 'MYRENDERER_'))
            and k not in ('VK_LAYER_PATH', 'VK_INSTANCE_LAYERS', 'VK_LAYER_SETTINGS_PATH')}


def scene_document(name, depth, mode):
    source = ROOT / 'assets/benchmarks/m3c' / (name + '.myscene')
    doc = json.loads(source.read_text(encoding='utf-8'))
    doc['renderer'].update(gpuPathTracingDepth=depth, gpuPathTracingSpp=64,
                           gpuTemporalDenoise=mode != 'raw', gpuSpatialDenoise=mode == 'denoised')
    for entity in doc['entities']:
        if 'model' in entity and not entity['model'].startswith('builtin:'):
            entity['model'] = (source.parent / entity['model']).resolve().as_posix()
    return doc


def telemetry():
    try:
        p = subprocess.run(['nvidia-smi', '--query-gpu=memory.used,temperature.gpu,clocks.gr',
                            '--format=csv,noheader,nounits'], capture_output=True, text=True, timeout=5)
        if p.returncode == 0:
            values = p.stdout.strip().splitlines()[0].split(',')
            return dict(adapterUsedMiB=float(values[0]), temperatureC=float(values[1]), clockMHz=float(values[2]))
    except (OSError, ValueError, subprocess.TimeoutExpired):
        pass
    return {}


def run_case(exe, out, scene, depth, mode, motion, repeat, idle=False):
    name = f'{scene}-d{depth}-{mode}-{motion}-{repeat}' + ('-idle' if idle else '')
    report_path, marker = out / (name + '.json'), out / (name + '-run.json')
    identity = hashlib.sha256(exe.read_bytes()).hexdigest()
    if marker.exists():
        record = json.loads(marker.read_text())
        if record['exeSha256'] != identity:
            raise RuntimeError('Executable changed; select a fresh output directory')
        if not report_path.exists() or hashlib.sha256(report_path.read_bytes()).hexdigest() != record['reportSha256']:
            raise RuntimeError('Previously checked report is missing or modified')
        return record
    doc = scene_document(scene, depth, mode)
    path = out / (name + '.myscene');path.write_text(json.dumps(doc, indent=2), encoding='utf-8')
    env = clean_environment()
    env.update(VK_LAYER_PATH=str(ROOT / '.local/tools/vulkan-validation-20261025'),
               IRIS_VULKAN_DISABLE_VALIDATION='1', IRIS_VULKAN_PATH_TRACER='1',
               IRIS_VULKAN_DISABLE_VSYNC='1', IRIS_BENCHMARK_FRAME_TRACE='1',
               IRIS_BENCHMARK_PT_SPP='4' if idle else ('1' if motion == 'moving' else '64'),
               MYRENDERER_BENCHMARK_WARMUP='4', MYRENDERER_BENCHMARK_FRAMES='30',
               MYRENDERER_BENCHMARK_OUTPUT=str(report_path), MYRENDERER_MSAA='1',
               MYRENDERER_RENDER_WIDTH='1280', MYRENDERER_RENDER_HEIGHT='720',
               MYRENDERER_EDITOR_WINDOW_WIDTH='1920', MYRENDERER_EDITOR_WINDOW_HEIGHT='1080',
               IRIS_BENCHMARK_PT_MOTION='1' if motion == 'moving' else '',
               IRIS_STATE_DIRECTORY=str(out / (name + '-state')))
    if motion != 'moving':
        env.pop('IRIS_BENCHMARK_PT_MOTION', None)
    if repeat == 1 and not idle:
        env['IRIS_BENCHMARK_PT_EXPORT'] = str(out / name)
    samples = [telemetry()];finished = threading.Event()
    def poll():
        while not finished.wait(1):
            samples.append(telemetry())
    thread = threading.Thread(target=poll, daemon=True);thread.start()
    print('BEGIN ' + name, flush=True);start = time.monotonic()
    try:
        with (out / (name + '.log')).open('w', encoding='utf-8') as log:
            p = subprocess.run([str(exe), str(path)], cwd=ROOT, env=env, stdout=log,
                               stderr=subprocess.STDOUT, timeout=300)
    finally:
        finished.set();thread.join(timeout=6)
    assert p.returncode == 0, (name, p.returncode)
    report = json.loads(report_path.read_text(encoding='utf-8'))
    assert (report['width'], report['height']) == (1280, 720)
    assert report['ptDepth'] == depth and not report['validationEnabled'] and not report['vsync']
    assert report['cpuFrameMeasurements'] == 30 and len(report['frameTrace']) == 34
    trace = report['frameTrace'][4:]
    assert all(t['width'] == 1280 and t['height'] == 720 and not t['validationErrors'] for t in trace)
    if idle:
        assert report['gpuFrameMeasurements'] == 0 and report['completedCpuFrameMeasurements'] == 30
    else:
        assert report['gpuFrameMeasurements'] == report['activeCpuFrameMeasurements'] == 30
        assert all(t['gpuTimingValid'] and t['displayUploads'] == 1 for t in trace)
        assert all(t['spp'] == 1 for t in trace) if motion == 'moving' else trace[-1]['spp'] == 34
        assert report['vulkanCounters']['gpuActivePassMs']['measurements'] == 30
    record = dict(name=name, exeSha256=identity, seconds=time.monotonic()-start,
                  scene=scene, depth=depth, mode=mode, motion=motion, repeat=repeat, idle=idle,
                  adapterTelemetry=samples, report=report_path.name,
                  reportSha256=hashlib.sha256(report_path.read_bytes()).hexdigest())
    marker.write_text(json.dumps(record, indent=2), encoding='utf-8')
    print('PASS ' + name, flush=True);return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build-ci-msvc/Release/Iris.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pilot', action='store_true')
    args = parser.parse_args();out = args.output.resolve();out.mkdir(parents=True, exist_ok=True)
    protocol = dict(exeSha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                    scenes={s: scene_inputs('assets/benchmarks/m3c/' + s + '.myscene') for s in SCENES},
                    shaders={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/'shaders/vulkan').glob('*.spv')},
                    width=1280, height=720, warmup=4, measured=30, repeats=2,
                    motion='yaw=authored+.2degrees*(frameIndex+1); target unchanged',
                    timing='CPU full UI wall frame; disjoint completed Vulkan passes exclude GL/UI/present; vsync and validation OFF')
    protocol_path = out/'protocol.json'
    if protocol_path.exists():
        assert json.loads(protocol_path.read_text()) == protocol, 'Protocol or inputs changed; select a fresh directory'
    else:
        protocol_path.write_text(json.dumps(protocol, indent=2), encoding='utf-8')
    if args.pilot:
        run_case(args.exe.resolve(), out, 'stanford-glass', 4, 'denoised', 'moving', 1);return
    for scene in SCENES:
        for mode in ['raw', 'accumulated', 'denoised']:
            for motion in ['static', 'moving']:
                for repeat in [1, 2]:run_case(args.exe.resolve(), out, scene, 4, mode, motion, repeat)
    for mode in ['raw', 'denoised']:
        for motion in ['static', 'moving']:
            for repeat in [1, 2]:run_case(args.exe.resolve(), out, 'stanford-glass', 3, mode, motion, repeat)
    for repeat in [1, 2]:run_case(args.exe.resolve(), out, 'stanford-glass', 4, 'denoised', 'static', repeat, True)


if __name__ == '__main__':
    main()
