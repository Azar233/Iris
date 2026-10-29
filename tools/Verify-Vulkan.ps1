#requires -Version 7.0
param(
    [ValidateSet('msvc','gcc')][string]$Compiler='msvc',
    [Parameter(Mandatory=$true)][string]$LayerDirectory,
    [switch]$Raster,
    [switch]$RayQuery,
    [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
if($Raster -and $RayQuery){throw 'Choose either -Raster or -RayQuery'}
$root=Split-Path $PSScriptRoot -Parent
$layerPath=(Resolve-Path -LiteralPath $LayerDirectory).Path
if(!(Test-Path -LiteralPath (Join-Path $layerPath 'VkLayer_khronos_validation.json'))){
    throw 'LayerDirectory must contain VkLayer_khronos_validation.json and its runtime library'
}
$buildName=@{msvc='build-ci-msvc';gcc='build-mingw'}[$Compiler]
if(!$OutputDirectory){$OutputDirectory=".local/verification/vulkan-$Compiler"}
$outputPath=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $outputPath | Out-Null
Push-Location $root
try {
    $cache=Get-Content -LiteralPath (Join-Path $buildName 'CMakeCache.txt')
    if(!($cache -contains 'IRIS_ENABLE_VULKAN:BOOL=ON')){throw "Enable IRIS_ENABLE_VULKAN in the $Compiler preset first"}
    $target=if($RayQuery){'IrisVulkanRayQueryAcceptance'}elseif($Raster){'IrisVulkanRasterAcceptance'}else{'IrisVulkanSmoke'}
    & cmake --build $buildName --config Release --target $target --parallel 6 *> (Join-Path $outputPath 'build.log')
    if($LASTEXITCODE -ne 0){throw "Vulkan build failed; see $outputPath/build.log"}
    $program=Join-Path $root ($buildName+$(if($Compiler -eq 'msvc'){'/Release/'}else{'/'})+$target+'.exe')
    # The layer search path applies only to the child. No registry or user
    # environment changes, and no parallel GPU/build/resource-copy operation.
    $runPath=Join-Path $outputPath ('run-'+[Guid]::NewGuid().ToString('N'))
    $arguments=if($Raster -or $RayQuery){@($root,$runPath)}else{@('--require-sync-validation','--output',$runPath)}
    & cmake -E env "VK_LAYER_PATH=$layerPath" $program @arguments *> (Join-Path $outputPath 'smoke.log')
    if($LASTEXITCODE -ne 0){throw "Vulkan synchronization validation failed; see $outputPath/smoke.log"}
    $report=Get-Content -LiteralPath (Join-Path $runPath 'report.json') -Raw | ConvertFrom-Json
    $valid=if($Raster -or $RayQuery){$report.coreValidation -and $report.synchronizationValidation -and $report.errors -eq 0 -and (!$RayQuery -or $report.oracleChecked -gt 1000)}else{$report.validationEnabled -and $report.synchronizationValidation -and $report.validationErrors -eq 0}
    if(!$valid -or (Select-String -LiteralPath (Join-Path $outputPath 'smoke.log') -Pattern '^Vulkan:' -Quiet)){
        throw 'The GPU run did not confirm core and synchronization validation without errors'
    }
    $summary=[ordered]@{compiler=$Compiler;target=$target;layerDirectory=$layerPath;runDirectory=$runPath;report=$report}
    $summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $outputPath 'verified.json') -Encoding utf8
    Write-Output "Vulkan $Compiler core/synchronization validation PASS; raster=$Raster; rayQuery=$RayQuery; evidence $outputPath"
} finally {Pop-Location}
