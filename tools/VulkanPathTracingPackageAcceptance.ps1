#requires -Version 7.0
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$ExpectedDirectory
)
$ErrorActionPreference='Stop'
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$expected=(Resolve-Path -LiteralPath $ExpectedDirectory).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $output | Out-Null
$empty=Join-Path $output 'empty-layers'
New-Item -ItemType Directory -Force $empty | Out-Null
foreach($file in @('Iris.exe','IrisBatch.exe','ASSET_LICENSES.md',
    'assets/benchmarks/m3c/sponza/credits_license.txt',
    'assets/benchmarks/m3c/stanford/SOURCE-README.txt',
    'shaders/vulkan/pt.rgen.spv','shaders/vulkan/pt.rmiss.spv','shaders/vulkan/pt.rchit.spv')) {
    if(!(Test-Path -LiteralPath (Join-Path $package $file))){throw "Package missing $file"}
}
if(@(Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object {$_.Name -match '^(VkLayer.*|vulkan-1)\.dll$'}).Count){throw 'Driver/validation DLL must not be bundled'}
$environment=@('--unset=IRIS_VULKAN_REQUIRE_VALIDATION','--unset=IRIS_VULKAN_CAPTURE',
    "VK_LAYER_PATH=$empty","VK_IMPLICIT_LAYER_PATH=$empty","IRIS_STATE_DIRECTORY=$output/state")
function Invoke-Product($name,$exe,$arguments,$extra=@(),$code=0) {
    & cmake -E env @environment @extra $exe @arguments *> "$output/$name.log"
    if($LASTEXITCODE -ne $code){throw "$name exit $LASTEXITCODE, expected $code"}
}
function Require-Same($left,$right) {
    if((Get-FileHash -LiteralPath $left).Hash -ne (Get-FileHash -LiteralPath $right).Hash){throw "PNG mismatch: $left / $right"}
}
Push-Location $package
try {
    foreach($case in @(@('22_gpu_sponza_atrium','sponza-atrium'),@('23_gpu_stanford_glass','stanford-glass'),@('24_gpu_stanford_opaque','stanford-opaque'))) {
        $job="assets/renderjobs/$($case[0]).renderjob"
        $name=$case[1]
        Invoke-Product $name './IrisBatch.exe' @('render-frame',$job,'0','--output',"$output/$name/frame")
        $report=Get-Content "$output/$name/frame-report.json" -Raw | ConvertFrom-Json
        if($report.renderer -ne 'gpu-path-traced' -or $report.gpuBackend.pipeline -ne 'VK_KHR_ray_tracing_pipeline' -or $report.gpuBackend.validation){throw 'Standalone GPU backend/layer mismatch'}
        if(@(Get-ChildItem "$output/$name" -File | Where-Object Extension -in '.png','.exr').Count -ne 16){throw 'Standalone AOV outputs incomplete'}
        if(!$report.gpuBackend.inputs.Count){throw 'Standalone input manifest is empty'}
        foreach($file in $report.gpuBackend.inputs) {
            if(!([IO.Path]::GetFullPath($file.path).StartsWith($package+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase))){throw "Source fallback: $($file.path)"}
        }
        Require-Same "$output/$name/frame.png" "$expected/$name/frame_0000.png"
    }
    $scene='assets/benchmarks/m3c/stanford-glass.myscene'
    $gpu=@('IRIS_VULKAN_PATH_TRACER=1','MYRENDERER_SMOKE_TEST=1','MYRENDERER_EDITOR_WINDOW_WIDTH=1920','MYRENDERER_EDITOR_WINDOW_HEIGHT=1080')
    Invoke-Product 'gui' './Iris.exe' @($scene) ($gpu+@("IRIS_GPU_PT_EXPORT=$output/gui","MYRENDERER_EDITOR_SCREENSHOT=$output/gui-workspace.png",'MYRENDERER_EDITOR_SCREENSHOT_WARMUP=240'))
    Invoke-Product 'save-reopen' './Iris.exe' @($scene) @('MYRENDERER_SMOKE_TEST=1',"MYRENDERER_SCENE_ROUNDTRIP=$output/saved.myscene")
    if(!(Select-String -LiteralPath "$output/save-reopen.log" -SimpleMatch 'GUI Scene save / reopen: PASS' -Quiet)){throw 'Standalone Scene save/reopen failed'}
    Invoke-Product 'reopened' './Iris.exe' @("$output/saved.myscene") ($gpu+@("IRIS_GPU_PT_EXPORT=$output/reopened"))
    Require-Same "$output/gui.png" "$output/reopened.png"
    $shader=Join-Path $package 'shaders/vulkan/pt.rgen.spv'
    $hidden=$shader+'.missing-test'
    Move-Item -LiteralPath $shader -Destination $hidden
    try {
        Invoke-Product 'missing-spv' './IrisBatch.exe' @('render-frame','assets/renderjobs/23_gpu_stanford_glass.renderjob','0','--output',"$output/missing/frame") @() 70
        if(Test-Path "$output/missing/frame.png"){throw 'Missing SPV published output'}
    } finally {Move-Item -LiteralPath $hidden -Destination $shader}
    [ordered]@{status='Passed';package=$package;buildId=$report.gpuBackend.buildId;cases=3;aovFiles=48;sourceFallback=$false;saveReopenPngIdentical=$true;missingSpvRejected=$true} |
        ConvertTo-Json | Set-Content "$output/report.json" -Encoding utf8
} finally {Pop-Location}
Write-Output "GPU PT standalone package PASS: $output"

