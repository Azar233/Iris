#requires -Version 7.0
param(
    [string]$BuildDirectory='build-ci-msvc',
    [Parameter(Mandatory=$true)][string]$LayerDirectory,
    [string]$OutputDirectory='.local/verification/m3b/product',
    [switch]$Physics,
    [switch]$Performance,
    [switch]$CancelOnly
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=[IO.Path]::GetFullPath($BuildDirectory)
$output=[IO.Path]::GetFullPath($OutputDirectory)
$layer=(Resolve-Path -LiteralPath $LayerDirectory).Path
New-Item -ItemType Directory -Force $output | Out-Null
$editor=Join-Path $build $(if(Test-Path "$build/Release/Iris.exe"){'Release/Iris.exe'}else{'Iris.exe'})
function Invoke-Gpu($name,$arguments,$environment=@{},$expected=0) {
    $settings=@("VK_LAYER_PATH=$layer",'IRIS_VULKAN_REQUIRE_VALIDATION=1',"IRIS_STATE_DIRECTORY=$output/state")
    foreach($key in $environment.Keys){$settings+=($key+'='+$environment[$key])}
    & cmake -E env @settings $editor @arguments *> "$output/$name.log"
    if($LASTEXITCODE -ne $expected){throw "$name failed: exit $LASTEXITCODE, expected $expected"}
    if(Select-String -LiteralPath "$output/$name.log" -Pattern '^Vulkan:' -Quiet){throw "$name emitted Vulkan diagnostics"}
}
function Require-Same($left,$right){if((Get-FileHash $left).Hash -ne (Get-FileHash $right).Hash){throw "Image mismatch: $left / $right"}}
function Test-Cancel {
    $name='Local\IrisRayQueryCancel-'+[Guid]::NewGuid().ToString('N')
    $event=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$name)
    $process=$null
    try {
        $start=[Diagnostics.ProcessStartInfo]::new('cmake');$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WorkingDirectory=$root
        $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
        foreach($argument in @('-E','env',"VK_LAYER_PATH=$layer",'IRIS_VULKAN_REQUIRE_VALIDATION=1',"IRIS_STATE_DIRECTORY=$output/state",$editor,'raster-sequence','assets/renderjobs/20_ray_query_rigid_bodies.renderjob','--output',"$output/cancel/frame_{frame:04}",'--cancel-event',$name)){$start.ArgumentList.Add($argument)}
        $process=[Diagnostics.Process]::Start($start);$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        $deadline=[DateTime]::UtcNow.AddSeconds(30)
        while(!(Test-Path "$output/cancel/frame_0000.png") -and !$process.HasExited -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 50}
        if(!(Test-Path "$output/cancel/frame_0000.png")){throw 'No active Ray Query frame before cancellation deadline'}
        [void]$event.Set()
        if(!$process.WaitForExit(10000)){$process.Kill($true);throw 'Ray Query cancellation timed out'}
        ($stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()) | Set-Content "$output/cancel.log" -Encoding utf8
        if($process.ExitCode -ne 130 -or !(Test-Path "$output/cancel/frame_0000-report.json")){throw 'Ray Query cancellation status / completed first frame mismatch'}
        if((Select-String -LiteralPath "$output/cancel.log" -Pattern '^Vulkan:' -Quiet) -or @(Get-ChildItem "$output/cancel" -Filter '*.partial*').Count){throw 'Ray Query cancellation diagnostics / partial output'}
        $first=Get-Content "$output/cancel/frame_0000-report.json" -Raw | ConvertFrom-Json
        if(!$first.vulkan.shadows -or !$first.vulkan.synchronizationValidation){throw 'Cancelled sequence did not exercise strictly validated Ray Query'}
        @{status='Passed';exitCode=130;rayQueryShadows=$true;partialOutputs=$false} | ConvertTo-Json | Set-Content "$output/cancel.json" -Encoding utf8
    } finally {if($process){if(!$process.HasExited){$process.Kill($true)};$process.Dispose()};$event.Dispose()}
}
Push-Location $root
try {
    if($CancelOnly){Test-Cancel;Write-Output 'Ray Query active cancellation PASS';return}
    $fixture=Join-Path $root 'assets/scenes/fixtures/38_ray_query_shadows.myscene'
    Invoke-Gpu 'job' @('raster-sequence','assets/renderjobs/19_ray_query_shadows.renderjob','--output',"$output/job/frame_{frame:04}")
    $report=Get-Content "$output/job/frame_0000-report.json" -Raw | ConvertFrom-Json
    if($report.backend -ne 'vulkan' -or $report.vulkan.shadowMethod -ne 'ray-query-directional-hard' -or !$report.vulkan.synchronizationValidation -or $report.vulkan.blasCount -ne 2 -or $report.vulkan.instanceCount -ne 3){throw 'Job backend/AS contract failed'}
    Invoke-Gpu 'gui' @($fixture) @{
        IRIS_VULKAN_RASTER='1';MYRENDERER_SMOKE_TEST='1';MYRENDERER_RENDER_WIDTH='640';MYRENDERER_RENDER_HEIGHT='360';
        MYRENDERER_TIMELINE_FPS='30';MYRENDERER_TIMELINE_FRAME='0';MYRENDERER_SCREENSHOT="$output/gui.png"
    }
    Require-Same "$output/gui.png" "$output/job/frame_0000.png"
    $scene=Get-Content $fixture -Raw | ConvertFrom-Json
    foreach($entity in $scene.entities){if(!$entity.model.StartsWith('builtin:')){$entity.model=[IO.Path]::GetFullPath((Join-Path (Split-Path $fixture) $entity.model)).Replace('\','/')}}
    $job=Get-Content assets/renderjobs/19_ray_query_shadows.renderjob -Raw | ConvertFrom-Json
    foreach($variant in @('off','csm','visibility')) {
        $scene.renderer.vulkanRayQueryShadows=$variant -ne 'off'
        $scene.renderer | Add-Member -NotePropertyName vulkanShadowDebug -NotePropertyValue ($variant -eq 'visibility') -Force
        $scene | ConvertTo-Json -Depth 24 | Set-Content "$output/$variant.myscene" -Encoding utf8
        $job.scene="$output/$variant.myscene".Replace('\','/');$job.output.path="$output/$variant/frame_{frame:04}".Replace('\','/')
        $job.raster.backend=if($variant -eq 'csm'){'opengl'}else{'vulkan'}
        $job | ConvertTo-Json -Depth 24 | Set-Content "$output/$variant.renderjob" -Encoding utf8
        Invoke-Gpu $variant @('raster-sequence',"$output/$variant.renderjob")
    }
    if((Get-FileHash "$output/off/frame_0000.png").Hash -eq (Get-FileHash "$output/job/frame_0000.png").Hash){throw 'Ray Query shadows have no image effect'}
    Invoke-Gpu 'missing-capability' @('raster-sequence','assets/renderjobs/19_ray_query_shadows.renderjob','--output',"$output/missing/frame_{frame:04}") @{IRIS_VULKAN_DISABLE_RAY_QUERY='1'} 1
    if(Test-Path "$output/missing/frame_0000.png"){throw 'Missing capability published a frame'}
    foreach($size in @(@(1440,900),@(1100,680))) {
        $name="workspace-$($size[0])"
        Invoke-Gpu $name @($fixture) @{
            IRIS_VULKAN_RASTER='1';IRIS_VULKAN_RAY_QUERY_UI_TEST='1';MYRENDERER_SMOKE_TEST='1';
            MYRENDERER_EDITOR_WINDOW_WIDTH=$size[0];MYRENDERER_EDITOR_WINDOW_HEIGHT=$size[1];
            MYRENDERER_EDITOR_SCREENSHOT="$output/$name.png";MYRENDERER_EDITOR_SCREENSHOT_TAB='renderer';MYRENDERER_EDITOR_SCREENSHOT_WARMUP='24'
        }
        foreach($message in @('enabled=0 debug=0','enabled=1 debug=0','enabled=1 debug=1')){
            if(!(Select-String -LiteralPath "$output/$name.log" -Pattern "Ray Query shadows UI: $message" -Quiet)){throw 'Actual Ray Query checkbox interaction failed'}
        }
        if(!(Test-Path "$output/$name.png")){throw 'Workspace screenshot missing'}
    }
    if($Physics) {
        foreach($run in @('a','b')){Invoke-Gpu "physics-$run" @('raster-sequence','assets/renderjobs/20_ray_query_rigid_bodies.renderjob','--output',"$output/physics-$run/frame_{frame:04}")}
        $updates=0
        for($index=0;$index -le 120;++$index){
            $stem='frame_{0:0000}' -f $index
            Require-Same "$output/physics-a/$stem.png" "$output/physics-b/$stem.png"
            $a=Get-Content "$output/physics-a/$stem-report.json" -Raw | ConvertFrom-Json
            $b=Get-Content "$output/physics-b/$stem-report.json" -Raw | ConvertFrom-Json
            if(!$a.moduleRun.contentHash -or "$($a.moduleRun.contentHash)" -ne "$($b.moduleRun.contentHash)" -or !$a.vulkan.shadows -or !$a.vulkan.synchronizationValidation){throw 'Physics shared pose/backend metadata mismatch'}
            if($index -gt 0 -and $a.vulkan.blasBuilds){throw 'Rigid transform rebuilt BLAS'}
            $updates+=$a.vulkan.tlasUpdates
        }
        if($updates -lt 10){throw 'P1 sequence did not exercise TLAS updates'}
        Invoke-Gpu 'physics-workspace' @('assets/scenes/fixtures/39_ray_query_rigid_bodies.myscene') @{
            IRIS_VULKAN_RASTER='1';IRIS_PHYSICS_WORKBENCH_TEST='1';IRIS_PHYSICS_TEMPORAL_DIRECTORY="$output/temporal";
            MYRENDERER_RENDER_WIDTH='640';MYRENDERER_RENDER_HEIGHT='360'
        }
        Require-Same "$output/temporal/phase-32.png" "$output/temporal/phase-35.png"
        Require-Same "$output/temporal/phase-33.png" "$output/temporal/phase-36.png"
        Require-Same "$output/temporal/phase-32.png" "$output/physics-a/frame_0000.png"
        Require-Same "$output/temporal/phase-33.png" "$output/physics-a/frame_0001.png"
        Test-Cancel
    }
    if($Performance) {
        foreach($variant in @('csm','off','rayquery')) {
            $input=if($variant -eq 'rayquery'){$fixture}else{"$output/$variant.myscene"}
            $environment=@{MYRENDERER_BENCHMARK_FRAMES='32';MYRENDERER_BENCHMARK_WARMUP='4';MYRENDERER_BENCHMARK_OUTPUT="$output/benchmark-$variant.json";MYRENDERER_RENDER_WIDTH='640';MYRENDERER_RENDER_HEIGHT='360';MYRENDERER_TIMELINE_FPS='30';MYRENDERER_TIMELINE_FRAME='0'}
            if($variant -ne 'csm'){$environment.IRIS_VULKAN_RASTER='1'}
            Invoke-Gpu "benchmark-$variant" @($input) $environment
            $measurement=Get-Content "$output/benchmark-$variant.json" -Raw | ConvertFrom-Json
            if($measurement.gpuFrameMeasurements -lt 28){throw 'Benchmark lacks GPU samples'}
        }
    }
    [ordered]@{backend='vulkan';shadowMethod='ray-query-directional-hard';guiJobPixelsIdentical=$true;actualCheckboxes=$true;windowWidths=@(1440,1100);physics=$Physics.IsPresent;performance=$Performance.IsPresent;buildId=$report.buildId} |
        ConvertTo-Json | Set-Content "$output/summary.json" -Encoding utf8
    Write-Output "Vulkan Ray Query product PASS; physics=$Physics; performance=$Performance"
} finally {Pop-Location}
