"""M3-C Sponza/Stanford fixed-camera linear EXR convergence.
Requires Python with NumPy/OpenCV (EXR enabled), only for development evidence.
"""
import argparse, json, os, subprocess, time
from pathlib import Path
os.environ['OPENCV_IO_ENABLE_OPENEXR']='1'
import cv2
import numpy as np
parser=argparse.ArgumentParser();parser.add_argument('--bin',required=True);parser.add_argument('--output',required=True);parser.add_argument('--layers',required=True);parser.add_argument('--gpu-tag',default='final')
a=parser.parse_args();root=Path(__file__).resolve().parent.parent;binary=Path(a.bin).resolve();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
# Declared before rendering. Absolute radiance is not clamped or tone mapped.
# CPU 256 SPP measures the reference's own Monte Carlo noise at this budget.
protocol=dict(resolution=[96,54],depth=8,seed=20261026,gpuSpp=[16,64,256],cpuReferenceSpp=2048,cpuComparisonSpp=256,
              maximumNormalizedRmse=.18,maximumCpuNoiseMultiple=2.5,maximumMeanLuminanceRelativeError=.12,
              require256BetterThan16=True)
(out/'protocol.json').write_text(json.dumps(protocol,indent=2))
env=os.environ.copy();env['VK_LAYER_PATH']=str(Path(a.layers).resolve());env['IRIS_VULKAN_REQUIRE_VALIDATION']='1';env['IRIS_STATE_DIRECTORY']=str(out/'state')
def run(job,name,renderer,spp):
    folder=out/name;stem=folder/'frame';report=folder/'frame-report.json'
    authored=dict(job);authored['scene']=str((root/'assets/renderjobs'/job['scene']).resolve());authored['renderer']=renderer;authored['resolution']=protocol['resolution'];authored['sampling']=dict(spp=spp,maxDepth=8,seed=protocol['seed']);authored['output']=dict(path=str(stem),formats=['png','exr'],resume=False)
    path=out/(name+'.renderjob');path.write_text(json.dumps(authored,indent=2))
    elapsed=None
    if not report.exists():
        start=time.perf_counter()
        with (out/(name+'.log')).open('w') as log:
            result=subprocess.run([str(binary),'render-frame',str(path),'0'],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT)
        elapsed=time.perf_counter()-start
        if result.returncode:raise RuntimeError(f'{name} exit {result.returncode}')
    evidence=json.loads(report.read_text());image=cv2.imread(str(stem)+'.exr',cv2.IMREAD_UNCHANGED)
    if image is None or image.shape!=(54,96,3) or not np.isfinite(image).all():raise RuntimeError('Invalid linear image '+name)
    for aov in ('albedo','normal','depth','direct','indirect','sample-count','variance'):
        plane=cv2.imread(str(stem)+'-'+aov+'.exr',cv2.IMREAD_UNCHANGED)
        if plane is None or not np.isfinite(plane).all():raise RuntimeError('Invalid AOV '+aov+' in '+name)
    counts=cv2.imread(str(stem)+'-sample-count.exr',cv2.IMREAD_UNCHANGED)
    if counts is None or not np.all(counts==spp):raise RuntimeError('Invalid committed SPP '+name)
    if renderer=='gpu-path-traced':
        backend=evidence['gpuBackend']
        if backend['validationErrors'] or backend['validationWarnings'] or not backend['synchronizationValidation']:raise RuntimeError('Core/Sync diagnostic '+name)
    return image.astype(np.float64),evidence,elapsed
results=[]
for number in (22,23,24):
    file=next((root/'assets/renderjobs').glob(str(number)+'_*.renderjob'));job=json.loads(file.read_text());name=file.stem
    print('Rendering '+name,flush=True)
    reference,cpu,cpuSeconds=run(job,name+'-cpu2048','cpu-path-traced',2048)
    cpu256,cpu256report,cpu256Seconds=run(job,name+'-cpu256','cpu-path-traced',256)
    scale=float(np.sqrt(np.mean(reference**2)));lum=np.array([.0722,.7152,.2126]);referenceLum=float(np.mean(reference@lum))
    row=dict(scene=job['scene'],referenceScale=scale,cpu256Rmse=float(np.sqrt(np.mean((cpu256-reference)**2))),
             cpu2048WallSeconds=cpuSeconds,cpu256WallSeconds=cpu256Seconds,samples=[])
    for spp in protocol['gpuSpp']:
        image,report,seconds=run(job,name+'-gpu'+str(spp)+'-'+a.gpu_tag,'gpu-path-traced',spp)
        rmse=float(np.sqrt(np.mean((image-reference)**2)));mean=float(np.mean(image@lum));backend=report['gpuBackend']
        row['samples'].append(dict(spp=spp,rmse=rmse,normalizedRmse=rmse/max(scale,1e-12),meanLuminance=mean,
                                  relativeMeanError=abs(mean-referenceLum)/max(referenceLum,1e-12),wallSeconds=seconds,gpu=backend))
    last=row['samples'][-1]
    row['passed']=(last['rmse']<row['samples'][0]['rmse']
                   and last['normalizedRmse']<protocol['maximumNormalizedRmse']
                   and last['rmse']<protocol['maximumCpuNoiseMultiple']*row['cpu256Rmse']
                   and last['relativeMeanError']<protocol['maximumMeanLuminanceRelativeError'])
    results.append(row);(out/('report-'+a.gpu_tag+'.json')).write_text(json.dumps(dict(protocol=protocol,scenes=results,allPassed=all(r['passed'] for r in results)),indent=2));print(json.dumps({k:v for k,v in row.items() if k!='samples'}),flush=True);print([(r['spp'],r['rmse'],r['relativeMeanError']) for r in row['samples']],flush=True)
if not all(r['passed'] for r in results):raise RuntimeError('M3-C reference convergence failed; retain outputs and diagnose')
print('Sponza and Stanford glass/control convergence PASS',flush=True)
