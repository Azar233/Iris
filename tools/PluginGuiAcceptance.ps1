param([string]$BuildDirectory='build-ci-msvc')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root $BuildDirectory
$bin=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release'}else{$build}
$exe=Join-Path $bin 'Iris.exe'
$out=Join-Path $build 'plugin-gui'
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {$saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)}
Push-Location $root
try {
    $env:MYRENDERER_SMOKE_TEST='1'
    $env:MYRENDERER_PLUGIN_ACTIVATION_TEST='1'
    & $exe assets/scenes/fixtures/15_stylized_clean_toon_gallery.myscene *> (Join-Path $out 'activation.log')
    if($LASTEXITCODE -ne 0 -or -not(Select-String -LiteralPath (Join-Path $out 'activation.log') -SimpleMatch 'transactional rejection: PASS' -Quiet)){throw 'Application activation/GPU regression failed'}
    Remove-Item Env:MYRENDERER_PLUGIN_ACTIVATION_TEST
    $env:MYRENDERER_EDITOR_SCREENSHOT_TAB='plugins';$env:MYRENDERER_EDITOR_SCREENSHOT_WARMUP='4'
    foreach($size in @(@(1440,900),@(1100,680))) {
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$size[0];$env:MYRENDERER_EDITOR_WINDOW_HEIGHT=[string]$size[1]
        foreach($scene in @(@('native','assets/scenes/fixtures/15_stylized_clean_toon_gallery.myscene'),@('fullscreen','assets/scenes/01_ocean_clouds_hero.myscene'))) {
            if(-not(Test-Path $scene[1])){throw "Missing acceptance scene: $($scene[1])"}
            $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out "ui-$($scene[0])-$($size[0])x$($size[1]).png"
            & $exe $scene[1] *> (Join-Path $out "ui-$($scene[0])-$($size[0]).log")
            if($LASTEXITCODE -ne 0 -or -not(Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){throw 'Plugin Inspector capture failed'}
        }
    }
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    $scene=Get-Content assets/scenes/fixtures/28_native_ocean_clouds.myscene -Raw | ConvertFrom-Json
    $scene.renderer | Add-Member -NotePropertyName renderPlugins -NotePropertyValue @{version=1;entries=@(@{id='iris.postprocess';enabled=$false})} -Force
    $scenePath=Join-Path $out 'disabled-native.myscene'
    $scene | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $scenePath
    $job=Get-Content assets/renderjobs/06_ocean_clouds_hero.renderjob -Raw | ConvertFrom-Json
    $job.scene=$scenePath;$jobPath=Join-Path $out 'disabled-native.renderjob'
    $job | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $jobPath
    $rejected=Join-Path $out ('rejected-'+[guid]::NewGuid().ToString('N').Substring(0,8))
    $ErrorActionPreference='Continue'
    & $exe raster-sequence $jobPath --output (Join-Path $rejected 'frame_{frame:04}') *> (Join-Path $out 'rejected-job.log')
    $exitCode=$LASTEXITCODE;$ErrorActionPreference='Stop'
    if($exitCode -ne 66 -or (Test-Path $rejected)){throw 'Disabled required plugin did not reject Job before output'}
    if(-not(Select-String -LiteralPath (Join-Path $out 'rejected-job.log') -SimpleMatch 'requires enabled render plugin: iris.postprocess' -Quiet)){throw 'Disabled plugin diagnostic missing'}
    $env:MYRENDERER_ANIMATION_TIME='1.25';$env:MYRENDERER_RENDER_WIDTH='1280';$env:MYRENDERER_RENDER_HEIGHT='720'
    $env:MYRENDERER_SCREENSHOT_WARMUP='64';$env:MYRENDERER_BENCHMARK_WARMUP='64';$env:MYRENDERER_BENCHMARK_FRAMES='240'
    foreach($case in @(@('native','assets/scenes/fixtures/15_stylized_clean_toon_gallery.myscene'),@('fullscreen','assets/scenes/01_ocean_clouds_hero.myscene'))){
        $env:MYRENDERER_SCREENSHOT=Join-Path $out "$($case[0])-fixed.png";$env:MYRENDERER_BENCHMARK_OUTPUT=Join-Path $out "$($case[0])-fixed.json"
        & $exe $case[1] *> (Join-Path $out "$($case[0])-fixed.log")
        if($LASTEXITCODE -ne 0 -or -not(Test-Path $env:MYRENDERER_SCREENSHOT) -or -not(Test-Path $env:MYRENDERER_BENCHMARK_OUTPUT)){throw 'Fixed output capture failed'}
    }
    if((Get-FileHash (Join-Path $out 'fullscreen-fixed.png')).Hash -ne (Get-FileHash (Join-Path $root 'assets/readme/glsl-ocean-hero.png')).Hash){throw 'Legacy fullscreen image changed'}
    Write-Output "Plugin GUI / live commands / GPU lifecycle / save-reopen / Job no-output rejection PASS: $out"
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
