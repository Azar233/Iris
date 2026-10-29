param(
    [string]$Build='build-ci-msvc/Release',
    [string]$Output='.local/verification/p1-close/rendering',
    [ValidateSet('Workspace','Temporal','Parity','Sequences','CpuSequences','RasterSequences','CompareSequences','Performance','Cancellation')][string]$Stage='Workspace'
)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path $PSScriptRoot -Parent
$binaryRoot=[IO.Path]::GetFullPath((Join-Path $sourceRoot $Build))
$outputRoot=[IO.Path]::GetFullPath((Join-Path $sourceRoot $Output))
New-Item -ItemType Directory -Force $outputRoot | Out-Null
function Invoke-Checked($name,$program,$arguments,$environment=@{},$measureMemory=$false) {
    $settings=@(('IRIS_STATE_DIRECTORY='+($outputRoot+'/state')),'MYRENDERER_LANGUAGE=en')
    foreach($key in $environment.Keys){$settings+=($key+'='+$environment[$key])}
    if($measureMemory) {
        $start=[Diagnostics.ProcessStartInfo]::new($program)
        $start.UseShellExecute=$false;$start.CreateNoWindow=$true
        $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
        $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
        foreach($argument in $arguments){$start.ArgumentList.Add([string]$argument)}
        foreach($setting in $settings){$entry=$setting.Split('=',2);$start.Environment[$entry[0]]=$entry[1]}
        $process=[Diagnostics.Process]::Start($start)
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        $peakWorkingSet=0L;$peakPrivate=0L
        while(!$process.WaitForExit(20)) {
            $process.Refresh()
            if(!$process.HasExited) {
                $peakWorkingSet=[Math]::Max($peakWorkingSet,$process.PeakWorkingSet64)
                $peakPrivate=[Math]::Max($peakPrivate,$process.PrivateMemorySize64)
            }
        }
        ($stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()) |
            Set-Content -LiteralPath (Join-Path $outputRoot ($name+'.log')) -Encoding utf8
        [ordered]@{peakWorkingSetBytes=$peakWorkingSet;sampledPeakPrivateBytes=$peakPrivate;pollMilliseconds=20} |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot ($name+'-memory.json')) -Encoding utf8
        if($process.ExitCode -ne 0){throw "$name failed ($($process.ExitCode))"}
        $process.Dispose();return
    }
    & cmake -E env @settings $program @arguments *> (Join-Path $outputRoot ($name+'.log'))
    if($LASTEXITCODE -ne 0){throw "$name failed ($LASTEXITCODE); see $outputRoot/$name.log"}
}
function Write-Job($name,$source,$outputFolder,$renderer='cpu-path-traced') {
    $job=Get-Content -LiteralPath (Join-Path $sourceRoot $source) -Raw | ConvertFrom-Json
    $job.scene=[IO.Path]::GetFullPath((Join-Path (Split-Path (Join-Path $sourceRoot $source)) $job.scene)).Replace('\','/')
    $job.output.path=($outputRoot+'/'+$outputFolder+'/frame_{frame:04}').Replace('\','/')
    if($job.PSObject.Properties['simulationCache']){$job.simulationCache=($outputRoot+'/cache.json').Replace('\','/')}
    $path=Join-Path $outputRoot ($name+'.renderjob')
    $job | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $path -Encoding utf8
    return $path
}
$editor=Join-Path $binaryRoot 'Iris.exe'
$batch=Join-Path $binaryRoot 'IrisBatch.exe'
$scene=Join-Path $sourceRoot 'assets/scenes/fixtures/33_cpu_rigid_bodies.myscene'
if($Stage -eq 'Temporal') {
    Invoke-Checked 'temporal' $editor @($scene) @{
        IRIS_PHYSICS_WORKBENCH_TEST='1';MYRENDERER_RENDER_PATH='1';MYRENDERER_MSAA='1'
        MYRENDERER_EDITOR_WINDOW_WIDTH='1440';MYRENDERER_EDITOR_WINDOW_HEIGHT='900'
        IRIS_PHYSICS_TEMPORAL_DIRECTORY=(Join-Path $outputRoot 'temporal')
    }
    foreach($phase in @(0,31,32,33,34,35,36)) {
        if(!(Test-Path -LiteralPath (Join-Path $outputRoot "temporal/phase-$phase.png"))){throw 'Missing temporal capture'}
    }
    foreach($pair in @(@(32,35),@(33,36))) {
        $left=(Get-FileHash -LiteralPath (Join-Path $outputRoot "temporal/phase-$($pair[0]).png")).Hash
        $right=(Get-FileHash -LiteralPath (Join-Path $outputRoot "temporal/phase-$($pair[1]).png")).Hash
        if($left -ne $right){throw 'Rewind/Reset and following-step TAA images differ'}
    }
}
if($Stage -eq 'Workspace') {
    foreach($size in @(@(1440,900),@(1100,680))) {
        foreach($tab in @('object','module')) {
            $name="workspace-$tab-$($size[0])x$($size[1])"
            Invoke-Checked $name $editor @($scene) @{
                MYRENDERER_SMOKE_TEST='1';IRIS_PHYSICS_WORKBENCH_TEST='1';IRIS_PHYSICS_UI_TEST=$tab
                MYRENDERER_EDITOR_WINDOW_WIDTH=$size[0];MYRENDERER_EDITOR_WINDOW_HEIGHT=$size[1]
                MYRENDERER_EDITOR_SCREENSHOT_TAB=$tab;MYRENDERER_EDITOR_SCREENSHOT_WARMUP='36'
                MYRENDERER_EDITOR_SCREENSHOT=(Join-Path $outputRoot ($name+'.png'))
            }
            if(!(Test-Path -LiteralPath (Join-Path $outputRoot ($name+'.png')))){throw 'Missing workspace screenshot'}
        }
    }
}
if($Stage -eq 'Parity') {
    $saved=Join-Path $outputRoot 'saved.myscene'
    Invoke-Checked 'gui-save-reopen' $editor @($scene) @{
        MYRENDERER_CPU_PREVIEW='1';MYRENDERER_CPU_PREVIEW_SCALE='3';MYRENDERER_CPU_PREVIEW_SPP='4'
        MYRENDERER_CPU_PREVIEW_DEPTH='4';MYRENDERER_CPU_PREVIEW_SEED='20261025';MYRENDERER_CPU_PREVIEW_AOV='0'
        MYRENDERER_CPU_PREVIEW_POWER_LIGHTS='0';MYRENDERER_CPU_PREVIEW_VNDF='0';MYRENDERER_CPU_PREVIEW_DENOISE='0'
        MYRENDERER_RENDER_WIDTH='320';MYRENDERER_RENDER_HEIGHT='180';MYRENDERER_TIMELINE_FRAME='30'
        MYRENDERER_TIMELINE_FPS='30';IRIS_PHYSICS_WORKBENCH_TEST='1';MYRENDERER_SCENE_ROUNDTRIP=$saved
        MYRENDERER_MODULE_REPORT=(Join-Path $outputRoot 'gui-report.json')
        MYRENDERER_CPU_PREVIEW_EXPORT=(Join-Path $outputRoot 'gui')
    }
    $jobPath=Write-Job 'parity' 'assets/renderjobs/12_cpu_rigid_bodies.renderjob' 'parity'
    $job=Get-Content -LiteralPath $jobPath -Raw | ConvertFrom-Json
    $job.scene=$saved.Replace('\','/');$job.PSObject.Properties.Remove('simulationCache')
    $job | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $jobPath -Encoding utf8
    Invoke-Checked 'batch-parity' $batch @('render-frame',$jobPath,'30')
    $gui=Get-Content -LiteralPath (Join-Path $outputRoot 'gui-report.json') -Raw | ConvertFrom-Json
    $rendered=Get-Content -LiteralPath (Join-Path $outputRoot 'parity/frame_0030-report.json') -Raw | ConvertFrom-Json
    foreach($field in @('id','apiVersion','buildId','seed','lastFrame','contentHash')) {
        if("$($gui.module.$field)" -ne "$($rendered.module.$field)"){throw "GUI/Batch $field mismatch"}
    }
    if($gui.fps -ne 30){throw 'GUI FPS mismatch'}
    Invoke-Checked 'gui-batch-image' (Join-Path $binaryRoot 'IrisImageComparison.exe') @(
        (Join-Path $outputRoot 'gui.png'),(Join-Path $outputRoot 'parity/frame_0030.png'),'0.00001','0.0')
}
if($Stage -in @('Sequences','CpuSequences')) {
    $cpuA=Write-Job 'cpu-a' 'assets/renderjobs/12_cpu_rigid_bodies.renderjob' 'cpu-a'
    Invoke-Checked 'simulate' $batch @('simulate',$cpuA)
    Invoke-Checked 'bake' $batch @('bake',$cpuA)
    Invoke-Checked 'cpu-a' $batch @('render-sequence',$cpuA)
    $cpuB=Write-Job 'cpu-b' 'assets/renderjobs/12_cpu_rigid_bodies.renderjob' 'cpu-b'
    Invoke-Checked 'cpu-b' $batch @('render-sequence',$cpuB)
}
if($Stage -in @('Sequences','RasterSequences')) {
    $rasterA=Write-Job 'raster-a' 'assets/renderjobs/13_rigid_bodies_raster.renderjob' 'raster-a'
    Invoke-Checked 'raster-a' $editor @('raster-sequence',$rasterA)
    $rasterB=Write-Job 'raster-b' 'assets/renderjobs/13_rigid_bodies_raster.renderjob' 'raster-b'
    Invoke-Checked 'raster-b' $editor @('raster-sequence',$rasterB)
}
if($Stage -in @('Sequences','CompareSequences')) {
    for($frame=0;$frame -le 120;$frame++) {
        $stem='frame_{0:D4}' -f $frame
        foreach($backend in @('cpu','raster')) {
            $a=Join-Path $outputRoot "$backend-a/$stem.png";$b=Join-Path $outputRoot "$backend-b/$stem.png"
            if((Get-FileHash -LiteralPath $a).Hash -ne (Get-FileHash -LiteralPath $b).Hash){throw "$backend frame $frame image mismatch"}
        }
        $cpu=Get-Content -LiteralPath (Join-Path $outputRoot "cpu-a/$stem-report.json") -Raw | ConvertFrom-Json
        $raster=Get-Content -LiteralPath (Join-Path $outputRoot "raster-a/$stem-report.json") -Raw | ConvertFrom-Json
        if("$($cpu.module.contentHash)" -ne "$($raster.moduleRun.contentHash)"){throw "CPU/Raster frame $frame pose mismatch"}
        if($cpu.simulationCache.status -ne 'Hit' -or $cpu.module.physics.statistics.measured){throw 'Cache replay report mismatch'}
    }
}
if($Stage -eq 'Performance') {
    foreach($path in @(0,1)) {
        Invoke-Checked "display-performance-$path" $editor @($scene) @{
            MYRENDERER_BENCHMARK_FRAMES='120';MYRENDERER_BENCHMARK_WARMUP='20';MYRENDERER_RENDER_PATH=$path
            MYRENDERER_RENDER_WIDTH='640';MYRENDERER_RENDER_HEIGHT='360';IRIS_PHYSICS_WORKBENCH_TEST='1'
            MYRENDERER_BENCHMARK_OUTPUT=(Join-Path $outputRoot "display-performance-$path.json")
        } $true
    }
}
if($Stage -eq 'Cancellation') {
    $cancelJob=Write-Job 'cancel' 'assets/renderjobs/12_cpu_rigid_bodies.renderjob' 'cancel'
    $job=Get-Content -LiteralPath $cancelJob -Raw | ConvertFrom-Json
    $job.frames.end=10000000
    $job.simulationCache=($outputRoot+'/cancel-cache.json').Replace('\','/')
    $job | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $cancelJob -Encoding utf8
    [IO.File]::WriteAllText($job.simulationCache,'existing-complete-cache')
    $before=(Get-FileHash -LiteralPath $job.simulationCache).Hash
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class IrisConsoleCancel {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool FreeConsole();
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool AttachConsole(uint processId);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool SetConsoleCtrlHandler(IntPtr handler, bool add);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GenerateConsoleCtrlEvent(uint signal, uint group);
}
'@
    # The hidden child has its own console. Only this helper joins it; the Codex
    # console and unrelated processes are never targeted by the Ctrl+C event.
    $child=Start-Process -FilePath $batch -ArgumentList @('bake',('"'+$cancelJob+'"')) -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $outputRoot 'cancel-stdout.log') -RedirectStandardError (Join-Path $outputRoot 'cancel-stderr.log')
    try {
        Start-Sleep -Milliseconds 250
        [IrisConsoleCancel]::FreeConsole() | Out-Null
        if(![IrisConsoleCancel]::AttachConsole([uint32]$child.Id)){throw 'Cannot join isolated cancellation console'}
        [IrisConsoleCancel]::SetConsoleCtrlHandler([IntPtr]::Zero,$true) | Out-Null
        if(![IrisConsoleCancel]::GenerateConsoleCtrlEvent(0,0)){throw 'Cannot send isolated Ctrl+C'}
        if(!$child.WaitForExit(30000)){throw 'CLI did not cancel within 30 seconds'}
        if($child.ExitCode -ne 130){throw "CLI cancellation exit was $($child.ExitCode), expected 130"}
        if((Get-FileHash -LiteralPath $job.simulationCache).Hash -ne $before){throw 'Cancelled CLI replaced complete cache'}
        if(Test-Path -LiteralPath ($job.simulationCache+'.partial')){throw 'Cancelled CLI left partial cache'}
    } finally {
        [IrisConsoleCancel]::FreeConsole() | Out-Null
        if(!$child.HasExited){$child.Kill();$child.WaitForExit()}
        $child.Dispose()
    }
}
Write-Output "Physics $Stage acceptance: PASS ($outputRoot)"
