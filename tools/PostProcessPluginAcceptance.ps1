param([string]$BuildDirectory='build-ci-msvc',[switch]$ExpectUnavailable)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root $BuildDirectory
$bin=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release'}else{$build}
$exe=Join-Path $bin 'Iris.exe'
$out=Join-Path $build 'a2-plugin'
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {$saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)}
Push-Location $root
try{
    & (Join-Path $bin 'IrisRenderPluginContractTests.exe') *> (Join-Path $out 'contract.log')
    if($LASTEXITCODE -ne 0){throw 'Pass/resource contract failed'}
    if($ExpectUnavailable){
        $rejected=Join-Path $out ('rejected-native-'+[guid]::NewGuid().ToString('N').Substring(0,8))
        $ErrorActionPreference='Continue'
        & $exe raster-sequence assets/renderjobs/06_ocean_clouds_hero.renderjob --output (Join-Path $rejected 'frame_{frame:04}') *> (Join-Path $out 'off-job.log')
        $exitCode=$LASTEXITCODE;$ErrorActionPreference='Stop'
        if($exitCode -ne 66 -or (Test-Path $rejected)){throw 'Missing native postprocess did not reject without output'}
        if(-not(Select-String -LiteralPath (Join-Path $out 'off-job.log') -SimpleMatch 'unavailable render plugin: iris.postprocess' -Quiet)){throw 'Missing postprocess diagnostic absent'}
        $env:MYRENDERER_ANIMATION_TIME='1.25';$env:MYRENDERER_RENDER_WIDTH='1280';$env:MYRENDERER_RENDER_HEIGHT='720'
        $env:MYRENDERER_SCREENSHOT_WARMUP='64';$env:MYRENDERER_BENCHMARK_WARMUP='64';$env:MYRENDERER_BENCHMARK_FRAMES='30'
        $env:MYRENDERER_SCREENSHOT=Join-Path $out 'off-enscape.png';$env:MYRENDERER_BENCHMARK_OUTPUT=Join-Path $out 'off-enscape.json'
        & $exe assets/scenes/01_ocean_clouds_hero.myscene *> (Join-Path $out 'off-enscape.log')
        if($LASTEXITCODE -ne 0 -or -not(Test-Path $env:MYRENDERER_SCREENSHOT)){throw 'Independent fullscreen plugin failed without postprocess'}
        $reference=Join-Path $root 'assets/readme/glsl-ocean-hero.png'
        if((Get-FileHash $reference).Hash -ne (Get-FileHash $env:MYRENDERER_SCREENSHOT).Hash){throw 'Fullscreen output changed when native postprocess absent'}
        Write-Output 'Postprocess OFF: native rejection/no output and independent Enscape exact image PASS'
    }else{
        & (Join-Path $bin 'IrisPostProcessPluginGpuTests.exe') *> (Join-Path $out 'postprocess-gpu.log')
        if($LASTEXITCODE -ne 0){throw 'Second plugin GPU contract failed'}
        Write-Output 'Postprocess ON: borrowed HDR/depth, rejection, parameters and history lifecycle PASS'
    }
}finally{
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
