#requires -Version 7.0
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$ExpectedFrame,
    [switch]$RayQuery
)
$ErrorActionPreference='Stop'
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$expected=(Resolve-Path -LiteralPath $ExpectedFrame).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $output | Out-Null
$empty=Join-Path $output 'empty-layers'
New-Item -ItemType Directory -Force $empty | Out-Null
foreach($file in @('Iris.exe','IrisBatch.exe','licenses/Vulkan-Headers-LICENSE.md',
    'assets/renderjobs/17_vulkan_baseline.renderjob','assets/renderjobs/18_vulkan_rigid_bodies.renderjob',
    'assets/scenes/fixtures/37_vulkan_baseline.myscene',
    'shaders/vulkan/raster.vert.spv','shaders/vulkan/sky.vert.spv','shaders/vulkan/raster.frag.spv')) {
    if(!(Test-Path -LiteralPath (Join-Path $package $file))){throw "Package missing $file"}
}
if($RayQuery){foreach($file in @('shaders/vulkan/rayquery.frag.spv','assets/renderjobs/19_ray_query_shadows.renderjob','assets/renderjobs/20_ray_query_rigid_bodies.renderjob','assets/scenes/fixtures/38_ray_query_shadows.myscene','assets/scenes/fixtures/39_ray_query_rigid_bodies.myscene')){if(!(Test-Path -LiteralPath (Join-Path $package $file))){throw "Package missing $file"}}}
if(@(Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object {$_.Name -match '^(VkLayer.*|vulkan-1)\.dll$'}).Count){throw 'Driver/validation DLL must not be bundled'}
$environment=@('--unset=IRIS_VULKAN_REQUIRE_VALIDATION',"VK_LAYER_PATH=$empty","VK_IMPLICIT_LAYER_PATH=$empty","IRIS_STATE_DIRECTORY=$output/state")
Push-Location $package
try {
    $job=if($RayQuery){'assets/renderjobs/19_ray_query_shadows.renderjob'}else{'assets/renderjobs/17_vulkan_baseline.renderjob'}
    $scene=if($RayQuery){'assets/scenes/fixtures/38_ray_query_shadows.myscene'}else{'assets/scenes/fixtures/37_vulkan_baseline.myscene'}
    & cmake -E env @environment ./Iris.exe raster-sequence $job --output "$output/frame_{frame:04}" *> "$output/job.log"
    if($LASTEXITCODE -ne 0){throw 'Standalone Vulkan Job failed'}
    $report=Get-Content "$output/frame_0000-report.json" -Raw | ConvertFrom-Json
    if($report.backend -ne 'vulkan' -or $report.vulkan.validationEnabled){throw 'Standalone backend/layer state differs from requested configuration'}
    if($RayQuery -and $report.vulkan.shadowMethod -ne 'ray-query-directional-hard'){throw 'Standalone did not use Ray Query shadows'}
    foreach($file in $report.inputManifest.files){
        if(!([IO.Path]::GetFullPath($file.path).StartsWith($package+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase))){throw "Standalone consumed source-tree input: $($file.path)"}
    }
    if((Get-FileHash "$output/frame_0000.png").Hash -ne (Get-FileHash $expected).Hash){throw 'Standalone Vulkan image differs from accepted source build'}
    & cmake -E env @environment IRIS_VULKAN_RASTER=1 MYRENDERER_SMOKE_TEST=1 MYRENDERER_RENDER_WIDTH=640 MYRENDERER_RENDER_HEIGHT=360 MYRENDERER_TIMELINE_FPS=30 MYRENDERER_TIMELINE_FRAME=0 "MYRENDERER_SCREENSHOT=$output/gui.png" ./Iris.exe $scene *> "$output/gui.log"
    if($LASTEXITCODE -ne 0 -or (Get-FileHash "$output/gui.png").Hash -ne (Get-FileHash $expected).Hash){throw 'Standalone GUI parity failed'}
    # New/Open intentionally clears pending captures; keep the save/reopen check
    # separate, then render the persisted scene in a fresh Vulkan process.
    & cmake -E env @environment MYRENDERER_SMOKE_TEST=1 "MYRENDERER_SCENE_ROUNDTRIP=$output/saved.myscene" ./Iris.exe $scene *> "$output/reopen.log"
    if($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath "$output/reopen.log" -Pattern 'GUI Scene save / reopen: PASS' -Quiet)){throw 'Standalone save/reopen failed'}
    & cmake -E env @environment IRIS_VULKAN_RASTER=1 MYRENDERER_SMOKE_TEST=1 MYRENDERER_RENDER_WIDTH=640 MYRENDERER_RENDER_HEIGHT=360 MYRENDERER_TIMELINE_FPS=30 MYRENDERER_TIMELINE_FRAME=0 "MYRENDERER_SCREENSHOT=$output/reopened.png" ./Iris.exe "$output/saved.myscene" *> "$output/reopened.log"
    if($LASTEXITCODE -ne 0 -or (Get-FileHash "$output/reopened.png").Hash -ne (Get-FileHash $expected).Hash){throw 'Standalone reopened scene differs'}
    & ./IrisBatch.exe validate assets/renderjobs/12_cpu_rigid_bodies.renderjob *> "$output/cpu-job.log"
    if($LASTEXITCODE -ne 0){throw 'Standalone existing CPU Job validation failed'}
} finally {Pop-Location}
[ordered]@{status='Passed';package=$package;buildId=$report.buildId;backend=$report.backend;validationEnabled=$report.vulkan.validationEnabled;sourceFallback=$false;pngSHA256=(Get-FileHash "$output/frame_0000.png").Hash} |
    ConvertTo-Json | Set-Content "$output/package.json" -Encoding utf8
Write-Output "Vulkan standalone package: PASS ($package)"
