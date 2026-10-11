param([string]$BuildDirectory='build-ci-msvc', [switch]$MultiPass)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=if([IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory}else{Join-Path $root $BuildDirectory}
$exe=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release/Iris.exe'}else{Join-Path $build 'Iris.exe'}
$out=Join-Path $build $(if($MultiPass){'shader-scene-multipass'}else{'shader-scene-acceptance'})
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {$saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)}
Push-Location $root
try {
    $fixture=if($MultiPass){'assets/shader-scenes/feedback/feedback.shaderscene'}else{'assets/shader-scenes/orbit/orbit.shaderscene'}
    if($MultiPass){$env:MYRENDERER_SHADER_SCENE_MULTIPASS_ACCEPTANCE='1'}
    $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_ANIMATION_TIME='1.25'
    $env:MYRENDERER_RENDER_WIDTH='320';$env:MYRENDERER_RENDER_HEIGHT='180'
    foreach($size in @(@(1440,900),@(1100,680))) {
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$size[0];$env:MYRENDERER_EDITOR_WINDOW_HEIGHT=[string]$size[1]
        $env:MYRENDERER_SHADER_SCENE_ACCEPTANCE_DIRECTORY=Join-Path $out ('case-'+[guid]::NewGuid().ToString('N'))
        $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out "editor-$($size[0])x$($size[1]).png"
        $log=Join-Path $out "application-$($size[0]).log"
        $ErrorActionPreference='Continue'
        & $exe $fixture *> $log
        $exitCode=$LASTEXITCODE;$ErrorActionPreference='Stop'
        if($exitCode -ne 0 -or -not(Select-String -LiteralPath $log -SimpleMatch 'ShaderScene application open / compile rejection / retained pixels+history / save+reopen / mode switch PASS' -Quiet)){
            throw 'ShaderScene application transaction failed'
        }
        $case=$env:MYRENDERER_SHADER_SCENE_ACCEPTANCE_DIRECTORY
        if((Get-FileHash (Join-Path $case 'before.png')).Hash -ne (Get-FileHash (Join-Path $case 'retained.png')).Hash){throw 'Rejected scene pixels changed'}
    }
    Remove-Item Env:MYRENDERER_SHADER_SCENE_ACCEPTANCE_DIRECTORY
    Remove-Item Env:MYRENDERER_EDITOR_SCREENSHOT
    foreach($run in 1..2){
        $env:MYRENDERER_SCREENSHOT=Join-Path $out "repeat-$run.png"
        & $exe $fixture *> (Join-Path $out "repeat-$run.log")
        if($LASTEXITCODE -ne 0 -or -not(Test-Path $env:MYRENDERER_SCREENSHOT)){throw 'ShaderScene capture failed'}
    }
    if((Get-FileHash (Join-Path $out 'repeat-1.png')).Hash -ne (Get-FileHash (Join-Path $out 'repeat-2.png')).Hash){throw 'ShaderScene fixed capture differs'}
    $raster=Get-Content assets/renderjobs/07_glsl_ocean_clouds.renderjob -Raw | ConvertFrom-Json
    $raster.scene=Join-Path $root $fixture
    $jobPath=Join-Path $out 'unsupported-raster.renderjob'
    $raster | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $jobPath
    $rejected=Join-Path $out ('raster-rejected-'+[guid]::NewGuid().ToString('N'))
    $ErrorActionPreference='Continue'
    & $exe raster-sequence $jobPath --output (Join-Path $rejected 'frame_{frame:04}') *> (Join-Path $out 'raster-rejected.log')
    $exitCode=$LASTEXITCODE;$ErrorActionPreference='Stop'
    if($exitCode -ne 66 -or (Test-Path $rejected)){throw 'Unsupported Raster Job created outputs'}
    $cpuOut=Join-Path $out ('cpu-rejected-'+[guid]::NewGuid().ToString('N'))
    $cpuJob=@{format='MyRendererRenderJob';schemaVersion=1;scene=$raster.scene;renderer='cpu-path-traced';camera='scene';
        resolution=@(16,16);frames=@{start=0;end=0;fps=24};sampling=@{spp=1;maxDepth=1;seed=7};aovs=@('beauty');
        output=@{path=(Join-Path $cpuOut 'frame');formats=@('png');resume=$false};simulationCache='';failurePolicy='stop'}
    $cpuJobPath=Join-Path $out 'unsupported-cpu.renderjob'
    $cpuJob | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $cpuJobPath
    $batch=Join-Path (Split-Path $exe -Parent) 'IrisBatch.exe'
    $ErrorActionPreference='Continue'
    & $batch render-frame $cpuJobPath 0 *> (Join-Path $out 'cpu-rejected.log')
    $exitCode=$LASTEXITCODE;$ErrorActionPreference='Stop'
    if($exitCode -ne 70 -or (Test-Path $cpuOut) -or -not(Select-String -LiteralPath (Join-Path $out 'cpu-rejected.log') -SimpleMatch 'CPU path tracing does not support ShaderScene GLSL' -Quiet)){
        throw "Unsupported CPU Job rejection failed (exit $exitCode)"
    }
    Write-Output "ShaderScene application transactions / two window sizes / fixed PNG repeat PASS: $out"
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
