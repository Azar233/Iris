#requires -Version 7.0
param(
    [string]$BuildDirectory='build-ci-msvc',
    [Parameter(Mandatory=$true)][string]$LayerDirectory,
    [string]$OutputDirectory='.local/verification/m3a/product',
    [switch]$Physics
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=[IO.Path]::GetFullPath($BuildDirectory)
$output=[IO.Path]::GetFullPath($OutputDirectory)
$layer=(Resolve-Path -LiteralPath $LayerDirectory).Path
New-Item -ItemType Directory -Force $output | Out-Null
$binary=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release'}else{$build}
$editor=Join-Path $binary 'Iris.exe'
function Invoke-Gpu($name,$arguments,$environment=@{},$expected=0) {
    $settings=@("VK_LAYER_PATH=$layer",'IRIS_VULKAN_REQUIRE_VALIDATION=1',"IRIS_STATE_DIRECTORY=$output/state",'MYRENDERER_LANGUAGE=en')
    foreach($key in $environment.Keys){$settings+=($key+'='+$environment[$key])}
    & cmake -E env @settings $editor @arguments *> (Join-Path $output ($name+'.log'))
    if($LASTEXITCODE -ne $expected){throw "$name exit code $LASTEXITCODE, expected $expected; see $output/$name.log"}
    if(Select-String -LiteralPath (Join-Path $output ($name+'.log')) -Pattern '^Vulkan:' -Quiet){throw "$name emitted Vulkan validation diagnostics"}
}
function Require-Same($left,$right) {
    if((Get-FileHash -LiteralPath $left).Hash -ne (Get-FileHash -LiteralPath $right).Hash){throw "Image mismatch: $left and $right"}
}
Push-Location $root
try {
    $fixture=Join-Path $root 'assets/scenes/fixtures/37_vulkan_baseline.myscene'
    Invoke-Gpu 'job' @('raster-sequence','assets/renderjobs/17_vulkan_baseline.renderjob','--output',"$output/job/frame_{frame:04}")
    $frame=Get-Content -LiteralPath "$output/job/frame_0000-report.json" -Raw | ConvertFrom-Json
    if($frame.backend -ne 'vulkan' -or !$frame.vulkan.validationEnabled -or !$frame.vulkan.synchronizationValidation -or $frame.vulkan.validationErrors){throw 'Job did not use the strictly validated Vulkan backend'}
    Invoke-Gpu 'gui-parity' @($fixture) @{
        IRIS_VULKAN_RASTER='1';MYRENDERER_SMOKE_TEST='1';MYRENDERER_RENDER_WIDTH='640';MYRENDERER_RENDER_HEIGHT='360';
        MYRENDERER_TIMELINE_FPS='30';MYRENDERER_TIMELINE_FRAME='0';
        MYRENDERER_SCREENSHOT="$output/gui.png"
    }
    Require-Same "$output/gui.png" "$output/job/frame_0000.png"
    $lightScene=Get-Content -LiteralPath $fixture -Raw | ConvertFrom-Json
    foreach($entity in $lightScene.entities){$entity.model=[IO.Path]::GetFullPath((Join-Path (Split-Path $fixture) $entity.model)).Replace('\','/')}
    $lightJob=Get-Content -LiteralPath (Join-Path $root 'assets/renderjobs/17_vulkan_baseline.renderjob') -Raw | ConvertFrom-Json
    foreach($variant in @('off','spot')) {
        $lightScene.renderer.localLights[0].intensity=if($variant -eq 'off'){0}else{12}
        $lightScene.renderer.localLights[0].type=if($variant -eq 'off'){0}else{1}
        if($variant -eq 'spot'){$lightScene.renderer.localLights[0] | Add-Member -NotePropertyName direction -NotePropertyValue @(2,-3,-4) -Force}
        $lightScene | ConvertTo-Json -Depth 24 | Set-Content "$output/local-$variant.myscene" -Encoding utf8
        $lightJob.scene="$output/local-$variant.myscene".Replace('\','/');$lightJob.output.path="$output/local-$variant/frame_{frame:04}".Replace('\','/')
        $lightJob | ConvertTo-Json -Depth 24 | Set-Content "$output/local-$variant.renderjob" -Encoding utf8
        Invoke-Gpu "local-$variant" @('raster-sequence',"$output/local-$variant.renderjob")
    }
    foreach($variant in @('job','local-spot')) {
        if((Get-FileHash "$output/$variant/frame_0000.png").Hash -eq (Get-FileHash "$output/local-off/frame_0000.png").Hash){throw 'Point/Spot light produced no image contribution'}
    }
    $badScene=Get-Content -LiteralPath $fixture -Raw | ConvertFrom-Json
    $badScene.entities=@($badScene.entities[0]);$badScene.entities[0].model=(Join-Path $root 'assets/models/glass_material_test.gltf').Replace('\','/')
    $badScene | ConvertTo-Json -Depth 24 | Set-Content "$output/unsupported.myscene" -Encoding utf8
    $badJob=Get-Content -LiteralPath (Join-Path $root 'assets/renderjobs/17_vulkan_baseline.renderjob') -Raw | ConvertFrom-Json
    $badJob.scene="$output/unsupported.myscene".Replace('\','/');$badJob.output.path="$output/unsupported/frame_{frame:04}".Replace('\','/')
    $badJob | ConvertTo-Json -Depth 24 | Set-Content "$output/unsupported.renderjob" -Encoding utf8
    Invoke-Gpu 'unsupported' @('raster-sequence',"$output/unsupported.renderjob") @{} 1
    if(Test-Path "$output/unsupported/frame_0000.png"){throw 'Unsupported material published a result'}
    foreach($size in @(@(1440,900),@(1100,680))) {
        $name="workspace-$($size[0])"
        Invoke-Gpu $name @($fixture) @{
            IRIS_VULKAN_UI_TEST='1';MYRENDERER_SMOKE_TEST='1';
            MYRENDERER_TIMELINE_FPS='30';MYRENDERER_TIMELINE_FRAME='0';
            MYRENDERER_EDITOR_WINDOW_WIDTH=$size[0];MYRENDERER_EDITOR_WINDOW_HEIGHT=$size[1];
            MYRENDERER_EDITOR_SCREENSHOT="$output/$name.png";MYRENDERER_EDITOR_SCREENSHOT_WARMUP='25'
        }
        $vulkanSelections=@(Select-String -LiteralPath "$output/$name.log" -Pattern 'Vulkan actual Workspace backend selection PASS').Count
        if(!(Test-Path "$output/$name.png") -or $vulkanSelections -ne 2 -or !(Select-String -LiteralPath "$output/$name.log" -Pattern 'Vulkan Workspace return to Raster PASS' -Quiet)){throw 'Actual Workspace backend roundtrip or screenshot failed'}
    }
    if($Physics) {
        foreach($run in @('a','b')) {
            Invoke-Gpu "physics-$run" @('raster-sequence','assets/renderjobs/18_vulkan_rigid_bodies.renderjob','--output',"$output/physics-$run/frame_{frame:04}")
        }
        for($index=0;$index -le 120;++$index) {
            $stem='frame_{0:0000}' -f $index
            Require-Same "$output/physics-a/$stem.png" "$output/physics-b/$stem.png"
            $a=Get-Content "$output/physics-a/$stem-report.json" -Raw | ConvertFrom-Json
            $b=Get-Content "$output/physics-b/$stem-report.json" -Raw | ConvertFrom-Json
            if("$($a.moduleRun.contentHash)" -ne "$($b.moduleRun.contentHash)" -or $a.vulkan.validationErrors -or !$a.vulkan.synchronizationValidation){throw 'Vulkan physics repeat/validation failed'}
        }
        Invoke-Gpu 'physics-workspace' @('assets/scenes/fixtures/33_cpu_rigid_bodies.myscene') @{
            IRIS_VULKAN_RASTER='1';IRIS_PHYSICS_WORKBENCH_TEST='1';IRIS_PHYSICS_TEMPORAL_DIRECTORY="$output/temporal";
            MYRENDERER_RENDER_WIDTH='640';MYRENDERER_RENDER_HEIGHT='360'
        }
        Require-Same "$output/temporal/phase-32.png" "$output/temporal/phase-35.png"
        Require-Same "$output/temporal/phase-33.png" "$output/temporal/phase-36.png"
        Require-Same "$output/temporal/phase-32.png" "$output/physics-a/frame_0000.png"
        Require-Same "$output/temporal/phase-33.png" "$output/physics-a/frame_0001.png"
        $eventName='Local\IrisVulkanCancel-'+[Guid]::NewGuid().ToString('N')
        $cancelEvent=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$eventName)
        $process=$null
        try {
            $start=[Diagnostics.ProcessStartInfo]::new('cmake');$start.UseShellExecute=$false;$start.CreateNoWindow=$true
            $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$start.WorkingDirectory=$root
            $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
            foreach($argument in @('-E','env',"VK_LAYER_PATH=$layer",'IRIS_VULKAN_REQUIRE_VALIDATION=1',"IRIS_STATE_DIRECTORY=$output/cancel-state",
                $editor,'raster-sequence',(Join-Path $root 'assets/renderjobs/18_vulkan_rigid_bodies.renderjob'),'--output',"$output/cancel/frame_{frame:04}",'--cancel-event',$eventName)){$start.ArgumentList.Add($argument)}
            $process=[Diagnostics.Process]::Start($start)
            $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$timer=[Diagnostics.Stopwatch]::StartNew()
            while(!(Test-Path "$output/cancel/frame_0000.png") -and !$process.HasExited -and $timer.Elapsed.TotalSeconds -lt 45){[void]$process.WaitForExit(20)}
            [void]$cancelEvent.Set()
            if(!$process.WaitForExit(10000)){$process.Kill($true);throw 'Vulkan cancellation did not finish within 10 seconds'}
            ($stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()) | Set-Content "$output/cancel.log" -Encoding utf8
            if(Select-String -LiteralPath "$output/cancel.log" -Pattern '^Vulkan:' -Quiet){throw 'Vulkan cancellation emitted validation diagnostics'}
            if($process.ExitCode -ne 130 -or !(Test-Path "$output/cancel/frame_0000-report.json")){throw 'Vulkan cancellation did not stop an active sequence with a complete first frame'}
            if(@(Get-ChildItem "$output/cancel" -Filter '*.partial*').Count){throw 'Vulkan cancellation left partial output'}
        } finally {
            if($process){if(!$process.HasExited){$process.Kill($true)};$process.Dispose()}
            $cancelEvent.Dispose()
        }
    }
    [ordered]@{backend='vulkan';guiJobPixelsIdentical=$true;actualWorkspaceSelection=$true;windowWidths=@(1440,1100);physics=$Physics.IsPresent} |
        ConvertTo-Json | Set-Content "$output/summary.json" -Encoding utf8
    Write-Output "Vulkan product Workspace/Job/strict validation PASS; physics=$Physics; evidence $output"
} finally {Pop-Location}
