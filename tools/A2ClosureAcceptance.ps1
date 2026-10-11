param([string]$BuildDirectory='build-ci-msvc',[string]$OffBuildDirectory='build-stored-off')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root $BuildDirectory
$bin=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release'}else{$build}
$off=Join-Path $root ($OffBuildDirectory+'/Release')
$out=Join-Path $build 'a2-closure'
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {$saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)}
Push-Location $root
try {
    # Both implementations absent: each pipeline rejects before outputs exist.
    foreach($job in @('06_ocean_clouds_hero','07_glsl_ocean_clouds')) {
        $rejected=Join-Path $out ('rejected-'+[guid]::NewGuid().ToString('N'))
        $ErrorActionPreference='Continue'
        & (Join-Path $off 'Iris.exe') raster-sequence ('assets/renderjobs/'+$job+'.renderjob') --output (Join-Path $rejected 'frame_{frame:04}') *> (Join-Path $out ($job+'-off.log'))
        $exitCode=$LASTEXITCODE;$ErrorActionPreference='Stop'
        if($exitCode -ne 66 -or (Test-Path $rejected)){throw 'OFF pipeline produced output or missed capability rejection'}
    }
    $cpuOut=Join-Path $out ('cpu-'+[guid]::NewGuid().ToString('N'))
    $cpuJob=@{format='MyRendererRenderJob';schemaVersion=1;scene=(Join-Path $root 'assets/scenes/fixtures/15_stylized_clean_toon_gallery.myscene');renderer='cpu-path-traced';camera='scene';
        resolution=@(16,16);frames=@{start=0;end=0;fps=24};sampling=@{spp=1;maxDepth=1;seed=7};aovs=@('beauty');
        output=@{path=(Join-Path $cpuOut 'frame');formats=@('png');resume=$false};simulationCache='';failurePolicy='stop'}
    $cpuJobPath=Join-Path $out 'cpu-off.renderjob'
    $cpuJob | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $cpuJobPath
    & (Join-Path $off 'IrisBatch.exe') render-frame $cpuJobPath 0 *> (Join-Path $out 'cpu-off.log')
    if($LASTEXITCODE -ne 0 -or -not(Test-Path (Join-Path $cpuOut 'frame.png'))){throw 'CPU job failed without GPU plugins'}
    $env:MYRENDERER_ANIMATION_TIME='1.25';$env:MYRENDERER_RENDER_WIDTH='1280';$env:MYRENDERER_RENDER_HEIGHT='720'
    $env:MYRENDERER_SCREENSHOT_WARMUP='64';$env:MYRENDERER_BENCHMARK_WARMUP='64';$env:MYRENDERER_BENCHMARK_FRAMES='240'
    foreach($scene in @(@('native','assets/scenes/fixtures/15_stylized_clean_toon_gallery.myscene'),@('fullscreen','assets/scenes/03_enscape_ocean_study.myscene'))) {
        $env:MYRENDERER_SCREENSHOT=Join-Path $out ($scene[0]+'.png')
        $env:MYRENDERER_BENCHMARK_OUTPUT=Join-Path $out ($scene[0]+'.json')
        & (Join-Path $bin 'Iris.exe') $scene[1] *> (Join-Path $out ($scene[0]+'.log'))
        if($LASTEXITCODE -ne 0 -or -not(Test-Path $env:MYRENDERER_SCREENSHOT)){throw 'Final performance capture failed'}
        $report=Get-Content $env:MYRENDERER_BENCHMARK_OUTPUT -Raw | ConvertFrom-Json
        if($report.gpuFrameMeasurements -lt 120 -or $report.gpuFrameP95Ms -le 0){throw 'Incomplete GPU measurement evidence'}
    }
    $previous=Get-Content (Join-Path $build 'a2-plugin/after.json') -Raw | ConvertFrom-Json
    $current=Get-Content (Join-Path $out 'native.json') -Raw | ConvertFrom-Json
    $limit=$previous.gpuFrameP95Ms+[Math]::Max(0.5,0.1*$previous.gpuFrameP95Ms)
    if($current.gpuFrameP95Ms -gt $limit){throw 'Current native GPU P95 exceeds the existing A2 comparison gate'}
    @{nativeGpuP95Ms=$current.gpuFrameP95Ms;historicalA2GpuP95Ms=$previous.gpuFrameP95Ms;limitMs=$limit;offNoOutput='PASS';cpuOff='PASS'} |
        ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $out 'summary.json')
    Write-Output "A2 OFF pipelines / CPU Job / current performance evidence PASS: $out"
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
