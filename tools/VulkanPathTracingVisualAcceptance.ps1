#requires -Version 7.0
param(
    [string]$BuildDirectory='build-ci-msvc',
    [Parameter(Mandatory=$true)][string]$LayerDirectory,
    [string]$OutputDirectory='.local/verification/m3c/viewport-fill'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=[IO.Path]::GetFullPath($BuildDirectory)
$bin=if(Test-Path "$build/Release/Iris.exe"){Join-Path $build 'Release'}else{$build}
$layer=(Resolve-Path -LiteralPath $LayerDirectory).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $output | Out-Null
Push-Location $root
try {
    foreach($name in @('sponza-atrium','stanford-glass','stanford-opaque')) {
        # Native Viewport size fills the workspace; the renderer reports its own PT cap.
        $environment=@('--unset=MYRENDERER_RENDER_WIDTH','--unset=MYRENDERER_RENDER_HEIGHT',
            '--unset=IRIS_GPU_PT_QUEUE_TEST',"VK_LAYER_PATH=$layer",'IRIS_VULKAN_REQUIRE_VALIDATION=1',
            'MYRENDERER_SMOKE_TEST=1','IRIS_VULKAN_PATH_TRACER=1',
            'MYRENDERER_EDITOR_WINDOW_WIDTH=1920','MYRENDERER_EDITOR_WINDOW_HEIGHT=1080',
            'MYRENDERER_EDITOR_SCREENSHOT_TAB=renderer','MYRENDERER_EDITOR_SCREENSHOT_WARMUP=240',
            "IRIS_STATE_DIRECTORY=$output/$name-full-state","IRIS_GPU_PT_EXPORT=$output/$name-full",
            "MYRENDERER_EDITOR_SCREENSHOT=$output/$name-workspace-1920.png")
        & cmake -E env @environment "$bin/Iris.exe" "assets/benchmarks/m3c/$name.myscene" *> "$output/$name-workspace-1920.log"
        if($LASTEXITCODE -or !(Test-Path "$output/$name-workspace-1920.png") -or
            !(Select-String -LiteralPath "$output/$name-workspace-1920.log" -SimpleMatch 'workspace export PASS: SPP=64' -Quiet)) {
            throw "Full Viewport capture failed: $name"
        }
        if(Select-String -LiteralPath "$output/$name-workspace-1920.log" -Pattern '^Vulkan:' -Quiet){throw "Vulkan diagnostic in $name"}
    }
} finally {Pop-Location}
Write-Output "1920x1080 workspace captures complete at 64 SPP; inspect all three images: $output"
