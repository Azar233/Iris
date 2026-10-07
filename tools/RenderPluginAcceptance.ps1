param([string]$BuildDirectory='build-ci-msvc', [switch]$ExpectUnavailable)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root $BuildDirectory
$bin=if(Test-Path (Join-Path $build 'Release/Iris.exe')) { Join-Path $build 'Release' } else { $build }
$exe=Join-Path $bin 'Iris.exe'
$out=Join-Path $build 'plugin-validation'
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object { $saved[$_.Name]=$_.Value; Remove-Item -LiteralPath ('Env:'+$_.Name) }
Push-Location $root
try {
    & (Join-Path $bin 'IrisRenderPluginRegistryTests.exe') *> (Join-Path $out 'registry.log')
    if($LASTEXITCODE -ne 0){throw 'Plugin registry acceptance failed'}
    if($ExpectUnavailable) {
        $rejectedOutput=Join-Path $out 'rejected-job'
        if(Test-Path $rejectedOutput){throw 'Use a fresh rejected-job output directory'}
        # Windows PowerShell treats expected native stderr as NativeCommandError.
        $ErrorActionPreference='Continue'
        & $exe raster-sequence assets/renderjobs/07_glsl_ocean_clouds.renderjob --output (Join-Path $rejectedOutput 'frame_{frame:04}') *> (Join-Path $out 'disabled-job.log')
        $rejectionExit=$LASTEXITCODE
        $ErrorActionPreference='Stop'
        if($rejectionExit -ne 66){throw 'Unavailable plugin Job was not rejected before rendering'}
        if(Test-Path $rejectedOutput){throw 'Unavailable plugin Job published outputs'}
        if(-not (Select-String -LiteralPath (Join-Path $out 'disabled-job.log') -SimpleMatch 'unavailable render plugin: iris.enscape-study' -Quiet)){throw 'Unavailable plugin diagnostic missing'}
        $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_ANIMATION_TIME='1.25'
        $env:MYRENDERER_SCREENSHOT=Join-Path $out 'disabled-native.png'
        & $exe assets/scenes/fixtures/26_module_workflow.myscene *> (Join-Path $out 'disabled-native.log')
        if($LASTEXITCODE -ne 0 -or -not(Test-Path $env:MYRENDERER_SCREENSHOT)){throw 'Core renderer unavailable without plugin'}
        Write-Output 'Plugin OFF: registry, native renderer and no-output rejection PASS'
    } else {
        & (Join-Path $bin 'IrisRenderPluginGpuTests.exe') *> (Join-Path $out 'gpu-lifecycle.log')
        if($LASTEXITCODE -ne 0){throw 'Plugin GPU lifecycle failed'}
        $env:MYRENDERER_ANIMATION_TIME='1.25';$env:MYRENDERER_RENDER_WIDTH='1280';$env:MYRENDERER_RENDER_HEIGHT='720'
        $env:MYRENDERER_SCREENSHOT_WARMUP='64';$env:MYRENDERER_BENCHMARK_WARMUP='64';$env:MYRENDERER_BENCHMARK_FRAMES='240'
        $env:MYRENDERER_SCREENSHOT=Join-Path $out 'after.png';$env:MYRENDERER_BENCHMARK_OUTPUT=Join-Path $out 'after.json'
        & $exe assets/scenes/01_ocean_clouds_hero.myscene *> (Join-Path $out 'after.log')
        if($LASTEXITCODE -ne 0){throw 'Plugin capture failed'}
        $before=Join-Path $out 'before.png';$beforeReport=Join-Path $out 'before.json'
        if(-not(Test-Path $before) -or -not(Test-Path $beforeReport)){throw 'Capture checkpoint before.png / before.json here first'}
        if((Get-FileHash $before).Hash -ne (Get-FileHash $env:MYRENDERER_SCREENSHOT).Hash){throw 'Plugin migration changed fixed image'}
        $old=Get-Content $beforeReport -Raw | ConvertFrom-Json
        $new=Get-Content $env:MYRENDERER_BENCHMARK_OUTPUT -Raw | ConvertFrom-Json
        if($old.gpuFrameMeasurements -lt 120 -or $new.gpuFrameMeasurements -lt 120 -or $new.gpuFrameP95Ms -le 0){throw 'Incomplete GPU measurements'}
        $limit=$old.gpuFrameP95Ms+[Math]::Max(0.5,0.1*$old.gpuFrameP95Ms)
        if($new.gpuFrameP95Ms -gt $limit){throw 'Plugin GPU P95 exceeds fixed overhead gate'}
        @{ImageSha256=(Get-FileHash $before).Hash;BeforeCpuP95Ms=$old.cpuFrameP95Ms;AfterCpuP95Ms=$new.cpuFrameP95Ms;BeforeGpuP95Ms=$old.gpuFrameP95Ms;AfterGpuP95Ms=$new.gpuFrameP95Ms;GpuLimitMs=$limit;Lifecycle='PASS'} | ConvertTo-Json | Set-Content (Join-Path $out 'summary.json')
        Write-Output 'Plugin ON: lifecycle, exact image and performance gate PASS'
    }
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
