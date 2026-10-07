param([string]$BuildDirectory='build-ci-msvc')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "$BuildDirectory/Release/Iris.exe"
$cmp=Join-Path $root "$BuildDirectory/Release/MyRendererImageComparison.exe"
$out=Join-Path $root "$BuildDirectory/ocean-realism/$([guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {$saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)}
Push-Location $root
try {
    $study=Get-Content assets/scenes/03_enscape_ocean_study.myscene -Raw | ConvertFrom-Json
    $study.renderer | Add-Member -NotePropertyName enscapeCubeEnabled -NotePropertyValue $false
    $study | ConvertTo-Json -Depth 32 | Set-Content (Join-Path $out 'study-no-cube.myscene')
    $env:MYRENDERER_ANIMATION_TIME='1.25'
    $env:MYRENDERER_RENDER_WIDTH='1280';$env:MYRENDERER_RENDER_HEIGHT='720'
    $env:MYRENDERER_BENCHMARK_FRAMES='240';$env:MYRENDERER_BENCHMARK_WARMUP='64'
    $env:MYRENDERER_SCREENSHOT_WARMUP='64'
    foreach($case in @(@('hero','assets/scenes/01_ocean_clouds_hero.myscene'),@('repeat','assets/scenes/01_ocean_clouds_hero.myscene'),@('study','assets/scenes/03_enscape_ocean_study.myscene'),@('no-cube',(Join-Path $out 'study-no-cube.myscene')))) {
        $name=$case[0]
        $env:MYRENDERER_SCREENSHOT=Join-Path $out "$name.png"
        $env:MYRENDERER_BENCHMARK_OUTPUT=Join-Path $out "$name.json"
        & $exe $case[1] *> (Join-Path $out "$name.log")
        if($LASTEXITCODE -ne 0){throw "$name capture failed"}
        $report=Get-Content (Join-Path $out "$name.json") -Raw | ConvertFrom-Json
        if($report.gpuFrameMeasurements -lt 120 -or $report.gpuFrameP95Ms -le 0 -or $report.gpuFrameP95Ms -gt 16.67){throw "$name GPU budget/measurement failed"}
    }
    if((Get-FileHash (Join-Path $out 'hero.png')).Hash -ne (Get-FileHash (Join-Path $out 'repeat.png')).Hash){throw 'Fixed-time repeat differs'}
    & $cmp (Join-Path $out 'study.png') (Join-Path $out 'no-cube.png') 1 100 *> (Join-Path $out 'cube-comparison.log')
    if($LASTEXITCODE -ne 0){throw 'Cube comparison failed'}
    $metrics=Get-Content (Join-Path $out 'cube-comparison.log') -Raw
    $m=[regex]::Match($metrics,'MAE=([0-9.eE+-]+), changed=([0-9.eE+-]+)%')
    if(-not $m.Success -or [double]::Parse($m.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture) -lt 0.001){throw 'Cube visibility had no effect'}
    $env:MYRENDERER_SCENE_ROUNDTRIP=Join-Path $out 'roundtrip.myscene'
    & $exe assets/scenes/01_ocean_clouds_hero.myscene *> (Join-Path $out 'roundtrip.log')
    if($LASTEXITCODE -ne 0){throw 'Scene save/reopen failed'}
    $scene=Get-Content $env:MYRENDERER_SCENE_ROUNDTRIP -Raw | ConvertFrom-Json
    if(-not $scene.renderer.enscapeCubeShaderEnabled -or $scene.renderer.enscapeCubeEnabled -or -not $scene.renderer.enscapeNoiseReduction){throw 'Saved renderer selection/visibility differs'}
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($run in 'sequence','sequence-repeat') {
        & $exe raster-sequence assets/renderjobs/07_glsl_ocean_clouds.renderjob --output (Join-Path $out "$run/frame_{frame:04}") *> (Join-Path $out "$run.log")
        if($LASTEXITCODE -ne 0){throw "$run failed"}
        $frames=@(Get-ChildItem (Join-Path $out $run) -Filter 'frame_????.png')
        $reports=@(Get-ChildItem (Join-Path $out $run) -Filter '*-report.json')
        if($frames.Count -ne 24 -or $reports.Count -ne 24){throw 'Sequence incomplete'}
        foreach($file in $reports){
            $report=Get-Content $file.FullName -Raw | ConvertFrom-Json
            if(-not $report.glslOcean.enabled -or -not $report.glslOcean.noiseReduction -or $report.glslOcean.cubeEnabled -or $report.water.enabled -or $report.cloud.enabled -or -not $report.inputManifest.complete){throw 'Wrong render path or incomplete manifest'}
        }
    }
    foreach($frame in Get-ChildItem (Join-Path $out 'sequence') -Filter 'frame_????.png'){
        if((Get-FileHash $frame.FullName).Hash -ne (Get-FileHash (Join-Path $out ('sequence-repeat/'+$frame.Name))).Hash){throw 'Sequence repeat differs'}
    }
    $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_EDITOR_SCREENSHOT_TAB='renderer';$env:MYRENDERER_EDITOR_SCREENSHOT_WARMUP='4'
    foreach($size in @(@(1440,900),@(1100,680))){
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$size[0];$env:MYRENDERER_EDITOR_WINDOW_HEIGHT=[string]$size[1]
        $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out "ui-$($size[0])x$($size[1]).png"
        & $exe assets/scenes/01_ocean_clouds_hero.myscene *> (Join-Path $out "ui-$($size[0]).log")
        if($LASTEXITCODE -ne 0 -or -not (Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){throw 'Inspector capture failed'}
    }
    $summary=[pscustomobject]@{Directory=$out;FixedRepeat='PASS';SequenceFrames=24;IdenticalRepeatedFrames=24;Reports=48;CubeVisibility='PASS';Roundtrip='PASS';GuiSizes=@('1440x900','1100x680')}
    $summary | ConvertTo-Json | Set-Content (Join-Path $out 'summary.json')
    $summary | ConvertTo-Json | Set-Content (Join-Path (Split-Path $out -Parent) 'summary.json')
    $summary | ConvertTo-Json
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($name in $saved.Keys){Set-Item -LiteralPath ('Env:'+$name) -Value $saved[$name]}
}
Write-Output 'GLSL ocean acceptance: PASS'
