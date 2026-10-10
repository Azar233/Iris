param([string]$BuildDirectory='build-ci-msvc')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root $BuildDirectory
$bin=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release'}else{$build}
$exe=Join-Path $bin 'Iris.exe'
$batch=Join-Path $bin 'IrisBatch.exe'
$out=Join-Path $build 'plugin-parameters'
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {$saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)}
Push-Location $root
try {
    $env:MYRENDERER_SMOKE_TEST='1'; $env:MYRENDERER_PLUGIN_PARAMETER_TEST='1'
    & $exe assets/models/cube.obj *> (Join-Path $out 'gpu-parameters.log')
    if($LASTEXITCODE -ne 0 -or -not(Select-String -LiteralPath (Join-Path $out 'gpu-parameters.log') -SimpleMatch 'rejection / save-reopen: PASS' -Quiet)) {throw 'Plugin parameter application/GPU acceptance failed'}
    Remove-Item Env:MYRENDERER_PLUGIN_PARAMETER_TEST
    $env:MYRENDERER_EDITOR_SCREENSHOT_TAB='plugins'; $env:MYRENDERER_EDITOR_SCREENSHOT_WARMUP='4'
    foreach($size in @(@(1440,900),@(1100,680))) {
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$size[0]; $env:MYRENDERER_EDITOR_WINDOW_HEIGHT=[string]$size[1]
        foreach($scene in @(@('native','assets/scenes/fixtures/15_stylized_clean_toon_gallery.myscene'),@('fullscreen','assets/scenes/01_ocean_clouds_hero.myscene'))) {
            $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out "ui-$($scene[0])-$($size[0])x$($size[1]).png"
            & $exe $scene[1] *> (Join-Path $out "ui-$($scene[0])-$($size[0]).log")
            if($LASTEXITCODE -ne 0 -or -not(Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){throw 'Dynamic plugin panel capture failed'}
        }
    }
    Remove-Item Env:MYRENDERER_EDITOR_SCREENSHOT
    $env:MYRENDERER_EDITOR_SCREENSHOT_TAB='plugins'
    # The generic scene format is produced by the application's save-reopen test.
    $scene=Get-Content (Join-Path ([IO.Path]::GetTempPath()) 'IrisPluginParameterGpuAcceptance/saved.myscene') -Raw | ConvertFrom-Json
    $scene.renderer.enscapeCubeShaderEnabled=$false
    $scene.renderer.renderPlugins.entries=@(@{id='iris.enscape-study';enabled=$false})
    $disabled=Join-Path $out 'disabled-optional.myscene'
    $scene | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $disabled
    $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out 'ui-disabled-1100x680.png'
    & $exe $disabled *> (Join-Path $out 'ui-disabled.log')
    if($LASTEXITCODE -ne 0 -or -not(Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){throw 'Disabled plugin panel capture failed'}
    Remove-Item Env:MYRENDERER_EDITOR_SCREENSHOT
    # Malformed generic values must fail preflight, before creating Raster outputs.
    $scene.renderer.renderPluginParameters.entries[0].values[2].value='invalid-number'
    $invalid=Join-Path $out 'invalid-parameters.myscene'
    $scene | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $invalid
    $job=Get-Content assets/renderjobs/06_ocean_clouds_hero.renderjob -Raw | ConvertFrom-Json
    $job.scene=$invalid
    $jobPath=Join-Path $out 'invalid-parameters.renderjob'
    $job | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $jobPath
    $rejected=Join-Path $out ('rejected-'+[guid]::NewGuid().ToString('N'))
    $ErrorActionPreference='Continue'
    & $exe raster-sequence $jobPath --output (Join-Path $rejected 'frame_{frame:04}') *> (Join-Path $out 'invalid-job.log')
    $exitCode=$LASTEXITCODE; $ErrorActionPreference='Stop'
    if($exitCode -eq 0 -or (Test-Path $rejected)){throw 'Invalid parameters produced Raster outputs'}
    if(-not(Select-String -LiteralPath (Join-Path $out 'invalid-job.log') -SimpleMatch 'Plugin Float value required' -Quiet)){throw 'Invalid parameter diagnostic missing'}
    # CPU consumes the shared Scene loader, without requiring a GPU plugin instance.
    $cpu=Join-Path $out ('cpu-'+[guid]::NewGuid().ToString('N'))
    $cpuJob=@{format='MyRendererRenderJob';schemaVersion=1;scene=$disabled;renderer='cpu-path-traced';camera='scene';
        resolution=@(16,16);frames=@{start=0;end=0;fps=24};sampling=@{spp=1;maxDepth=1;seed=7};aovs=@('beauty');
        output=@{path=(Join-Path $cpu 'frame');formats=@('png');resume=$false};simulationCache='';failurePolicy='stop'}
    $cpuJobPath=Join-Path $out 'cpu-parameters.renderjob'
    $cpuJob | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $cpuJobPath
    & $batch render-frame $cpuJobPath 0 *> (Join-Path $out 'cpu-parameters.log')
    if($LASTEXITCODE -ne 0 -or -not(Test-Path (Join-Path $cpu 'frame.png'))){throw 'CPU did not accept generic parameter scene'}
    $cpuRejected=Join-Path $out ('cpu-rejected-'+[guid]::NewGuid().ToString('N'))
    $cpuJob.scene=$invalid; $cpuJob.output.path=Join-Path $cpuRejected 'frame'
    $cpuJob | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 $cpuJobPath
    $ErrorActionPreference='Continue'
    & $batch render-frame $cpuJobPath 0 *> (Join-Path $out 'cpu-invalid-parameters.log')
    $exitCode=$LASTEXITCODE; $ErrorActionPreference='Stop'
    if($exitCode -eq 0 -or (Test-Path $cpuRejected)){throw 'Invalid parameters produced CPU outputs'}
    if(-not(Select-String -LiteralPath (Join-Path $out 'cpu-invalid-parameters.log') -SimpleMatch 'Plugin Float value required' -Quiet)){throw 'CPU parameter diagnostic missing'}
    Write-Output "Plugin parameters GPU / dynamic panels / disabled state / Raster+CPU Job no-output rejection PASS: $out"
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
