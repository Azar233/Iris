param([string]$BuildDirectory='build-ci-msvc')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=if([IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory}else{Join-Path $root $BuildDirectory}
$exe=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release/Iris.exe'}else{Join-Path $build 'Iris.exe'}
$out=Join-Path $build 'enscape-package-application'
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {$saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)}
Push-Location $root
try {
    $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_ANIMATION_TIME='1.25'
    $env:MYRENDERER_RENDER_WIDTH='320';$env:MYRENDERER_RENDER_HEIGHT='180'
    foreach($size in @(@(1440,900),@(1100,680))) {
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$size[0];$env:MYRENDERER_EDITOR_WINDOW_HEIGHT=[string]$size[1]
        $env:MYRENDERER_ENSCAPE_SHADER_SCENE_ACCEPTANCE_DIRECTORY=Join-Path $out ('case-'+[guid]::NewGuid().ToString('N'))
        $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out "editor-$($size[0])x$($size[1]).png"
        $log=Join-Path $out "application-$($size[0]).log"
        $ErrorActionPreference='Continue'; & $exe assets/scenes/03_enscape_ocean_study.myscene *> $log
        $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
        if($code -ne 0 -or -not(Select-String -LiteralPath $log -SimpleMatch 'Enscape ShaderScene application 01/03 / four Passes / roundtrip / noiseReduction / native switch / failure+mode transaction PASS' -Quiet)) {
            throw "Enscape application acceptance failed: $log"
        }
    }
    Remove-Item Env:MYRENDERER_ENSCAPE_SHADER_SCENE_ACCEPTANCE_DIRECTORY
    Remove-Item Env:MYRENDERER_EDITOR_SCREENSHOT
    Remove-Item Env:MYRENDERER_SMOKE_TEST
    $env:MYRENDERER_SCREENSHOT_WARMUP='1'
    $env:MYRENDERER_BENCHMARK_FRAMES='30';$env:MYRENDERER_BENCHMARK_WARMUP='8'
    foreach($run in 1..2) {
        $env:MYRENDERER_SCREENSHOT=Join-Path $out "repeat-$run.png"
        $env:MYRENDERER_BENCHMARK_OUTPUT=Join-Path $out "timing-$run.json"
        & $exe assets/scenes/01_ocean_clouds_hero.myscene *> (Join-Path $out "repeat-$run.log")
        if($LASTEXITCODE -ne 0){throw 'Enscape migrated fixed PNG failed'}
        $timing=Get-Content $env:MYRENDERER_BENCHMARK_OUTPUT -Raw | ConvertFrom-Json
        if($timing.gpuFrameMeasurements -lt 16 -or $timing.gpuFrameP95Ms -le 0 -or $timing.waterEnabled){throw 'Migrated package lost GPU timing or fullscreen path metadata'}
    }
    if((Get-FileHash (Join-Path $out 'repeat-1.png')).Hash -ne (Get-FileHash (Join-Path $out 'repeat-2.png')).Hash){throw 'Enscape package repeated PNG differs'}
    # Preserve the existing Raster Job route: compatibility scenes use the package,
    # while standalone ShaderScene Jobs remain outside this slice.
    $job=Get-Content assets/renderjobs/07_glsl_ocean_clouds.renderjob -Raw | ConvertFrom-Json
    $job.scene=Join-Path $root 'assets/scenes/01_ocean_clouds_hero.myscene'
    $job.resolution=@(320,180);$job.frames.start=0;$job.frames.end=1
    $jobPath=Join-Path $out 'compatibility.renderjob'
    $job | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $jobPath
    Remove-Item Env:MYRENDERER_SCREENSHOT
    Remove-Item Env:MYRENDERER_BENCHMARK_OUTPUT
    $sequence=Join-Path $out ('sequence-'+[guid]::NewGuid().ToString('N'))
    & $exe raster-sequence $jobPath --output (Join-Path $sequence 'frame_{frame:04}') *> (Join-Path $out 'compatibility-job.log')
    if($LASTEXITCODE -ne 0 -or @(Get-ChildItem $sequence -Filter '*.png').Count -ne 2){throw 'Migrated compatibility Job lost Raster output'}
    $reports=@(Get-ChildItem $sequence -Filter '*report.json')
    if($reports.Count -ne 2){throw 'Compatibility Job omitted capture reports'}
    foreach($report in $reports) {
        $text=Get-Content -LiteralPath $report.FullName -Raw
        if($text -notmatch 'enscape.shaderscene' -or $text -notmatch 'weather.raw'){throw 'Compatibility capture omitted package resource identity'}
        foreach($input in @('pass_a.frag','pass_b.frag','pass_c.frag','pass_image.frag','buffer_a.glsl','buffer_b.glsl','buffer_c.glsl','image.glsl','inputs.glsl')) {
            if(-not $text.Contains($input)){throw "Compatibility capture omitted source: $input"}
        }
    }
    Write-Output "Enscape migrated application / commands / two windows / fixed PNG / legacy Raster Job PASS: $out"
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
