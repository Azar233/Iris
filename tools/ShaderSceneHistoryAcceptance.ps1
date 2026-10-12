param([string]$BuildDirectory='build-ci-msvc', [switch]$SingleWindow, [switch]$NarrowPanel)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=if([IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory}else{Join-Path $root $BuildDirectory}
$exe=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release/Iris.exe'}else{Join-Path $build 'Iris.exe'}
$batch=Join-Path (Split-Path $exe -Parent) 'IrisBatch.exe'
$out=Join-Path $root ('.local/verification/s1b5/'+(Split-Path $build -Leaf))
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -like 'MYRENDERER_*' -or $_.Name -like 'IRIS_*'} | ForEach-Object {
    $saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)
}
Push-Location $root
try {
    $env:IRIS_STATE_DIRECTORY=Join-Path $out 'state';New-Item -ItemType Directory -Force $env:IRIS_STATE_DIRECTORY | Out-Null
    Set-Content -LiteralPath (Join-Path $env:IRIS_STATE_DIRECTORY 'language') -Value en
    $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_ANIMATION_TIME='0.5'
    $env:MYRENDERER_RENDER_WIDTH='320';$env:MYRENDERER_RENDER_HEIGHT='180'
    $env:MYRENDERER_EDITOR_SCREENSHOT_TAB='assets';$env:MYRENDERER_EDITOR_SCREENSHOT_WARMUP='16'
    $env:IRIS_SHADER_HISTORY_UI_TEST='1'
    if($NarrowPanel){$env:IRIS_SHADER_PARAMETER_NARROW_PANEL='1'}
    $widths=if($SingleWindow -or $NarrowPanel){@(1440)}else{@(1440,1100)}
    foreach($width in $widths) {
        $case=Join-Path $out ('case-'+[guid]::NewGuid().ToString('N'))
        $env:IRIS_SHADER_HISTORY_ACCEPTANCE_DIRECTORY=$case
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$width;$env:MYRENDERER_EDITOR_WINDOW_HEIGHT=if($width -eq 1440){'900'}else{'680'}
        $label=if($NarrowPanel){'narrow'}else{[string]$width}
        $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out "history-$label.png"
        $log=Join-Path $out "application-$label.log"
        $ErrorActionPreference='Continue'; & $exe assets/shader-scenes/orbit/orbit.shaderscene *> $log
        $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
        if($code -ne 0 -or -not(Select-String -LiteralPath $log -SimpleMatch 'ShaderScene history commands' -Quiet) -or
            -not(Select-String -LiteralPath $log -SimpleMatch 'ShaderScene history real parameter+camera toggles: PASS' -Quiet) -or
            -not(Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){throw "History application/UI failed: $log"}
        $scene=Get-Content (Join-Path $case 'ui-saved.myscene') -Raw | ConvertFrom-Json
        if($scene.renderer.shaderSceneHistory.resetOnParameters -or $scene.renderer.shaderSceneHistory.resetOnCamera){throw 'UI policy not saved'}
    }
    Get-ChildItem Env: | Where-Object {$_.Name -like 'MYRENDERER_*' -or ($_.Name -like 'IRIS_*' -and $_.Name -ne 'IRIS_STATE_DIRECTORY')} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($backend in 'raster','cpu-path-traced') {
        $rejected=Join-Path $out ('rejected-'+[guid]::NewGuid().ToString('N'))
        $job=@{format='MyRendererRenderJob';schemaVersion=1;scene=(Join-Path $case 'saved.myscene');renderer=$backend;camera='scene';resolution=@(16,16);
            frames=@{start=0;end=0;fps=24};sampling=@{spp=1;maxDepth=1;seed=7};aovs=@('beauty');
            output=@{path=(Join-Path $rejected 'frame_{frame:04}');formats=@('png');resume=$false};simulationCache='';failurePolicy='stop'}
        $jobPath=Join-Path $out ($backend+'.renderjob');$job | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $jobPath
        $log=Join-Path $out ($backend+'-rejected.log');$ErrorActionPreference='Continue'
        if($backend -eq 'raster'){& $exe raster-sequence $jobPath --output (Join-Path $rejected 'frame_{frame:04}') *> $log;$expected=66}
        else {& $batch render-frame $jobPath 0 *> $log;$expected=70}
        $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
        $diagnostic=if($backend -eq 'raster'){'ShaderScene Render Jobs are not supported'}else{'CPU path tracing does not support ShaderScene GLSL'}
        if($code -ne $expected -or (Test-Path $rejected) -or -not(Select-String -LiteralPath $log -SimpleMatch $diagnostic -Quiet)) {
            throw "Unsupported ShaderScene Job did not reject cleanly: $backend ($code)"
        }
    }
    Write-Output "ShaderScene saved history / shared commands / real UI / unsupported Jobs PASS: $out"
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object {$_.Name -like 'MYRENDERER_*' -or $_.Name -like 'IRIS_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
