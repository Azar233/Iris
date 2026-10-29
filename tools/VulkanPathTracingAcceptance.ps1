#requires -Version 7.0
param(
    [string]$BuildDirectory='build-ci-msvc',
    [Parameter(Mandatory=$true)][string]$LayerDirectory,
    [string]$OutputDirectory='.local/verification/m3c1/product'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=[IO.Path]::GetFullPath($BuildDirectory)
$output=[IO.Path]::GetFullPath($OutputDirectory)
$layer=(Resolve-Path -LiteralPath $LayerDirectory).Path
New-Item -ItemType Directory -Force $output | Out-Null
$bin=if(Test-Path "$build/Release/Iris.exe"){Join-Path $build 'Release'}else{$build}
function Invoke-Gpu($name,$executable,$arguments,$environment=@{},$expected=0) {
    $settings=@("VK_LAYER_PATH=$layer",'IRIS_VULKAN_REQUIRE_VALIDATION=1',"IRIS_STATE_DIRECTORY=$output/state")
    foreach($key in $environment.Keys){$settings+=($key+'='+$environment[$key])}
    & cmake -E env @settings $executable @arguments *> "$output/$name.log"
    if($LASTEXITCODE -ne $expected){throw "$name exit $LASTEXITCODE, expected $expected; see $output/$name.log"}
    if(Select-String -LiteralPath "$output/$name.log" -Pattern '^Vulkan:' -Quiet){throw "$name emitted Vulkan diagnostics"}
}
function Require-Same($left,$right){if((Get-FileHash $left).Hash -ne (Get-FileHash $right).Hash){throw "Image mismatch: $left / $right"}}
Push-Location $root
try {
    Invoke-Gpu 'foundation' "$bin/IrisVulkanPathTracingAcceptance.exe" @($root,"$output/foundation")
    foreach($run in @('a','b')) {
        Invoke-Gpu "job-$run" "$bin/IrisBatch.exe" @('render-frame','assets/renderjobs/21_gpu_path_traced.renderjob','0','--output',"$output/job-$run/frame")
    }
    $images=@(Get-ChildItem "$output/job-a" -File | Where-Object Extension -in @('.png','.hdr','.exr'))
    if($images.Count -ne 24){throw 'Expected eight AOVs in three formats'}
    foreach($file in $images){Require-Same $file.FullName "$output/job-b/$($file.Name)"}
    $report=Get-Content "$output/job-a/frame-report.json" -Raw | ConvertFrom-Json
    if($report.renderer -ne 'gpu-path-traced' -or $report.gpuBackend.pipeline -ne 'VK_KHR_ray_tracing_pipeline' -or $report.gpuBackend.committedSamples -ne 64 -or !$report.gpuBackend.synchronizationValidation -or $report.gpuBackend.validationErrors){throw 'GPU Job metadata mismatch'}
    Invoke-Gpu 'gui' "$bin/Iris.exe" @('assets/scenes/fixtures/40_gpu_path_traced.myscene') @{
        IRIS_VULKAN_PATH_TRACER='1';IRIS_GPU_PT_EXPORT="$output/gui";MYRENDERER_RENDER_WIDTH='160';MYRENDERER_RENDER_HEIGHT='90'
    }
    Require-Same "$output/gui.png" "$output/job-a/frame.png"
    Require-Same "$output/gui.exr" "$output/job-a/frame.exr"
    Invoke-Gpu 'history' "$bin/Iris.exe" @('assets/scenes/fixtures/40_gpu_path_traced.myscene') @{
        IRIS_VULKAN_PATH_TRACER='1';IRIS_GPU_PT_HISTORY_TEST='1';MYRENDERER_RENDER_WIDTH='160';MYRENDERER_RENDER_HEIGHT='90'
    }
    foreach($input in @('camera','seed','light','tint')) {
        if(!(Select-String -LiteralPath "$output/history.log" -SimpleMatch "history reset: $input -> 1 SPP PASS" -Quiet)){throw "Missing history reset: $input"}
    }
    foreach($size in @(@(1440,900),@(1100,680))) {
        $name="workspace-$($size[0])"
        Invoke-Gpu $name "$bin/Iris.exe" @('assets/scenes/fixtures/40_gpu_path_traced.myscene') @{
            IRIS_GPU_PT_UI_TEST='1';MYRENDERER_SMOKE_TEST='1';MYRENDERER_RENDER_WIDTH='320';MYRENDERER_RENDER_HEIGHT='180';
            MYRENDERER_EDITOR_WINDOW_WIDTH=$size[0];MYRENDERER_EDITOR_WINDOW_HEIGHT=$size[1];
            MYRENDERER_EDITOR_SCREENSHOT="$output/$name.png";MYRENDERER_EDITOR_SCREENSHOT_TAB='renderer';MYRENDERER_EDITOR_SCREENSHOT_WARMUP='26'
        }
        foreach($message in @('backend selection: GPU Path Traced PASS','backend selection: Raster PASS','actual Restart sampling button PASS')) {
            if(!(Select-String -LiteralPath "$output/$name.log" -SimpleMatch $message -Quiet)){throw "Actual Workspace interaction missing: $message"}
        }
    }
    Invoke-Gpu 'missing-capability' "$bin/IrisBatch.exe" @('render-frame','assets/renderjobs/21_gpu_path_traced.renderjob','0','--output',"$output/missing/frame") @{IRIS_VULKAN_DISABLE_PATH_TRACING='1'} 70
    if(Test-Path "$output/missing/frame.png"){throw 'Missing capability published output'}
    @{status='Passed';device=$report.gpuBackend.device;buildId=$report.gpuBackend.buildId;all24AovFilesDeterministic=$true;guiJobPngExrIdentical=$true;strictCoreSync=$true;actualWorkspaceSizes=@('1440x900','1100x680');backgroundQueue='Unavailable in C1'} |
        ConvertTo-Json -Depth 5 | Set-Content "$output/report.json" -Encoding utf8
    Write-Output "GPU PT C1 product acceptance PASS: $output"
} finally {Pop-Location}
