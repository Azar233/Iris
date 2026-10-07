param([string]$BuildDirectory = 'build-ci-msvc', [ValidateSet('m2b','m2c','m2d')][string]$Stage='m2b')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "$BuildDirectory/Release/Iris.exe"
$comparator = Join-Path $root "$BuildDirectory/Release/MyRendererImageComparison.exe"
$outputRoot = Join-Path $root "$BuildDirectory/$Stage-scene-job"
# Raster output publication deliberately refuses overwrites; preserve earlier
# evidence and give each acceptance attempt its own output transaction.
$output = Join-Path $outputRoot ([guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Force -Path $output | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $output 'roundtrip') | Out-Null
$saved = @{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object { $saved[$_.Name]=$_.Value; Remove-Item -LiteralPath ('Env:'+$_.Name) }
Push-Location $root
try {
    $env:MYRENDERER_RENDER_QUEUE_STATE = Join-Path $output 'queue.json'
    foreach($run in 'first','repeat') {
        & $exe raster-sequence assets/renderjobs/06_ocean_clouds_hero.renderjob --output (Join-Path $output "$run/frame_{frame:04}") *> (Join-Path $output "$run.log")
        if ($LASTEXITCODE -ne 0) { throw "Raster Job $run failed" }
        $frames = @(Get-ChildItem (Join-Path $output $run) -Filter 'frame_????.png')
        $reports = @(Get-ChildItem (Join-Path $output $run) -Filter '*-report.json')
        if ($frames.Count -ne 24 -or $reports.Count -ne 24) { throw 'Incomplete sequence' }
        foreach($file in $reports) {
            $report = Get-Content $file.FullName -Raw | ConvertFrom-Json
            if($Stage -ne 'm2b' -and (-not $report.water.surfaceOptics -or $report.water.cloudReflectionStrength -ne 1)) {throw 'Scene/Job water input mismatch'}
            if (-not $report.inputManifest.complete -or $report.moduleSeed -ne 20261006 -or -not $report.cloud.heightLighting -or [Math]::Abs($report.cloud.shapeBlend - 0.85) -gt 0.00001 -or [Math]::Abs($report.timeSeconds - $report.frame / 24.0) -gt 0.000001) { throw 'Scene/Job cloud input mismatch' }
        }
    }
    foreach($frame in Get-ChildItem (Join-Path $output 'first') -Filter 'frame_????.png') {
        if ((Get-FileHash $frame.FullName).Hash -ne (Get-FileHash (Join-Path $output ('repeat/' + $frame.Name))).Hash) { throw "Repeat mismatch: $($frame.Name)" }
    }
    $env:MYRENDERER_BENCHMARK_FRAMES='1'
    $env:MYRENDERER_BENCHMARK_WARMUP='4'
    $env:MYRENDERER_BENCHMARK_OUTPUT=Join-Path $output 'roundtrip-profile.json'
    $env:MYRENDERER_TIMELINE_FRAME='0'
    $env:MYRENDERER_ANIMATION_FRAME_STEP='0'
    $env:MYRENDERER_CLOUD_TEMPORAL='0'
    $env:MYRENDERER_TAA='0'
    $env:MYRENDERER_DETERMINISM='1'
    $env:MYRENDERER_RENDER_WIDTH='1280'
    $env:MYRENDERER_RENDER_HEIGHT='720'
    $env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
    $env:MYRENDERER_SCREENSHOT=Join-Path $output 'roundtrip.png'
    $env:MYRENDERER_SCREENSHOT_WARMUP='4'
    $env:MYRENDERER_SCENE_ROUNDTRIP=Join-Path $output 'roundtrip/hero.myscene'
    & $exe assets/scenes/fixtures/28_native_ocean_clouds.myscene *> (Join-Path $output 'roundtrip.log')
    if ($LASTEXITCODE -ne 0) { throw 'GUI save/reopen failed' }
    $scene=Get-Content $env:MYRENDERER_SCENE_ROUNDTRIP -Raw | ConvertFrom-Json
    if($Stage -ne 'm2b' -and (-not $scene.renderer.waterSurfaceOptics -or $scene.renderer.waterCloudReflectionStrength -ne 1)) {throw 'Saved water optics missing'}
    if (-not $scene.renderer.cloudHeightLighting -or [Math]::Abs($scene.renderer.cloudShapeBlend-0.85) -gt 0.00001) { throw 'Saved cloud parameters missing' }
    # NewScene correctly cancels pending screenshots. Capture the saved scene
    # in a fresh invocation instead of weakening that cancellation contract.
    $savedScene = $env:MYRENDERER_SCENE_ROUNDTRIP
    Remove-Item Env:MYRENDERER_SCENE_ROUNDTRIP
    & $exe $savedScene *> (Join-Path $output 'saved-scene-capture.log')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $env:MYRENDERER_SCREENSHOT)) { throw 'Saved Scene capture failed' }
    & $comparator (Join-Path $output 'roundtrip.png') (Join-Path $output 'first/frame_0000.png') 0.00001 0 *> (Join-Path $output 'gui-job-comparison.log')
    if ($LASTEXITCODE -ne 0) { throw 'GUI/Job pixels differ at fixed time' }
    Remove-Item Env:MYRENDERER_SCREENSHOT,Env:MYRENDERER_BENCHMARK_FRAMES,Env:MYRENDERER_BENCHMARK_WARMUP,Env:MYRENDERER_BENCHMARK_OUTPUT
    $env:MYRENDERER_SMOKE_TEST='1'
    Remove-Item Env:MYRENDERER_RENDER_WIDTH,Env:MYRENDERER_RENDER_HEIGHT
    $env:MYRENDERER_EDITOR_SCREENSHOT_TAB='renderer'
    foreach($size in @(@(1440,900),@(1100,680))) {
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$size[0]
        $env:MYRENDERER_EDITOR_WINDOW_HEIGHT=[string]$size[1]
        $name="workspace-$($size[0])x$($size[1])"
        $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $output "$name.png"
        & $exe assets/scenes/fixtures/28_native_ocean_clouds.myscene *> (Join-Path $output "$name.log")
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)) { throw 'GUI capture failed' }
    }
    [pscustomobject]@{RunDirectory=$output;Frames=24;IdenticalRepeatedFrames=24;Reports=48;CloudHeightLighting=$true;ShapeBlend=0.85;GuiSaveReopen='PASS';GuiJobParity='PASS';GuiSizes=@('1440x900','1100x680')} | ConvertTo-Json | Set-Content (Join-Path $output 'summary.json')
    Copy-Item -LiteralPath (Join-Path $output 'summary.json') -Destination (Join-Path $outputRoot 'summary.json')
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object { Remove-Item -LiteralPath ('Env:'+$_.Name) }
    foreach($name in $saved.Keys) { Set-Item -LiteralPath ('Env:'+$name) -Value $saved[$name] }
}
Write-Output ($Stage.ToUpper()+' Scene/Job: PASS')
