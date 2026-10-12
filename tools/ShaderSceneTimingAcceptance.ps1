param([string]$BuildDirectory='build-ci-msvc',[switch]$SingleWindow,[switch]$ApplicationOnly,[switch]$ModeSwitchOnly,[string]$ReferenceDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root $BuildDirectory
$bin=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release'}else{$build}
$out=Join-Path $root ('.local/verification/s1d1/'+$BuildDirectory+'/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {
    $saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)
}
function Invoke-Check([string]$Executable,[string[]]$Arguments,[string]$Log){
    $info=New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName=Join-Path $bin $Executable;$info.Arguments=($Arguments | ForEach-Object {'"'+$_+'"'}) -join ' '
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WorkingDirectory=$root
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $process=New-Object System.Diagnostics.Process;$process.StartInfo=$info
    if(-not $process.Start()){throw "Cannot start $Executable"}
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    try {
        if(-not $process.WaitForExit(60000)){throw "Timed out: $Executable"}
        ($stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()) | Set-Content -Encoding UTF8 $Log
        if($process.ExitCode -ne 0){throw "Check failed ($($process.ExitCode)): $Log"}
    }finally{$process.Dispose()}
}
function Test-ModeSwitch {
    Get-ChildItem Env: | Where-Object {$_.Name -like 'MYRENDERER_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    $env:IRIS_STATE_DIRECTORY=Join-Path $out 'mode-state'
    New-Item -ItemType Directory -Force $env:IRIS_STATE_DIRECTORY | Out-Null
    $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_ANIMATION_TIME='1.25'
    $env:MYRENDERER_RENDER_WIDTH='320';$env:MYRENDERER_RENDER_HEIGHT='180'
    $env:MYRENDERER_SHADER_SCENE_ACCEPTANCE_DIRECTORY=Join-Path $out 'mode-switch'
    $log=Join-Path $out 'mode-switch.log'
    Invoke-Check 'Iris.exe' @((Join-Path $root 'assets/shader-scenes/orbit/orbit.shaderscene')) $log
    if(-not(Select-String -LiteralPath $log -SimpleMatch 'save+reopen / mode switch PASS' -Quiet)){throw 'Mode switch acceptance did not execute'}
}
try {
    if($ModeSwitchOnly){Test-ModeSwitch;Write-Output "ShaderScene/Native mode timing cleanup PASS: $out";return}
    if(-not $ApplicationOnly){
        $env:IRIS_SHADER_SCENE_COST_REPORT=Join-Path $out 'pass-cost.json'
        Invoke-Check 'IrisShaderSceneGpuTests.exe' @((Join-Path $root 'shaders')) (Join-Path $out 'shader-gpu.log')
        Remove-Item Env:IRIS_SHADER_SCENE_COST_REPORT
        $cost=Get-Content (Join-Path $out 'pass-cost.json') -Raw | ConvertFrom-Json
        if($cost.cases.Count -ne 4 -or @($cost.cases | Where-Object {$_.samples -ne 72}).Count){throw 'Incomplete dual-package/resolution cost samples'}
        Invoke-Check 'IrisEnscapeShaderSceneTests.exe' @((Join-Path $root 'shaders'),(Join-Path $out 'enscape')) (Join-Path $out 'enscape-gpu.log')
    }
    $env:IRIS_STATE_DIRECTORY=Join-Path $out 'state'
    New-Item -ItemType Directory -Force $env:IRIS_STATE_DIRECTORY | Out-Null
    Set-Content (Join-Path $env:IRIS_STATE_DIRECTORY 'language') en
    $widths=if($SingleWindow){@(1440)}else{@(1440,1100)}
    foreach($package in 'sequence','enscape'){
        $expected=if($package -eq 'sequence'){@('ShaderScene Accum','ShaderScene Image')}else{@('ShaderScene A','ShaderScene B','ShaderScene C','ShaderScene Image')}
        foreach($width in $widths){
            $env:MYRENDERER_BENCHMARK_FRAMES='72';$env:MYRENDERER_BENCHMARK_WARMUP='24'
            $env:MYRENDERER_ANIMATION_TIME='0.33333334'
            $env:MYRENDERER_RENDER_WIDTH='320';$env:MYRENDERER_RENDER_HEIGHT='180'
            $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$width
            $env:MYRENDERER_EDITOR_WINDOW_HEIGHT=if($width -eq 1440){'900'}else{'680'}
            $env:MYRENDERER_BENCHMARK_OUTPUT=Join-Path $out "$package-$width-profile.json"
            $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out "$package-$width.png"
            $env:MYRENDERER_EDITOR_SCREENSHOT_WARMUP='32'
            $env:MYRENDERER_EDITOR_SCREENSHOT_TAB='assets'
            Invoke-Check 'Iris.exe' @((Join-Path $root "assets/shader-scenes/$package/$package.shaderscene")) (Join-Path $out "$package-$width.log")
            $profile=Get-Content $env:MYRENDERER_BENCHMARK_OUTPUT -Raw | ConvertFrom-Json
            if($profile.width -ne 320 -or $profile.height -ne 180 -or $profile.gpuFrameMeasurements -lt 16 -or
                @($profile.gpuPasses.PSObject.Properties).Count -ne $expected.Count -or -not(Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){
                throw 'Application did not expose complete ShaderScene GPU profile'}
            foreach($name in $expected){$timing=$profile.gpuPasses.PSObject.Properties[$name].Value
                if(-not $timing -or $timing.measurements -lt 16 -or $timing.measurements -gt $profile.gpuFrameMeasurements -or
                    $timing.p95Ms -lt $timing.p50Ms -or $timing.p50Ms -lt 0){throw "Invalid application Pass timing: $name"}}
        }
        Get-ChildItem Env: | Where-Object {$_.Name -like 'MYRENDERER_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
        $example=if($package -eq 'sequence'){'08_shader_scene_sequence'}else{'09_enscape_shader_sequence'}
        $capture=Join-Path $out "$package-parity/frame_{frame:04}"
        Invoke-Check 'Iris.exe' @('raster-sequence',(Join-Path $root "assets/renderjobs/$example.renderjob"),'--output',$capture) (Join-Path $out "$package-parity.log")
        foreach($frame in 3..8){$stem='frame_{0:0000}' -f $frame
            $png=Join-Path $out "$package-parity/$stem.png"
            $report=Get-Content (Join-Path $out "$package-parity/$stem-report.json") -Raw | ConvertFrom-Json
            if(-not(Test-Path $png) -or -not $report.inputManifest.complete -or $report.shaderScene.seed -ne 7 -or
                $report.frame -ne $frame -or $report.fps -ne 24 -or -not $report.shaderScene.buildId){throw 'Incomplete fixed capture identity'}
            if($ReferenceDirectory -and (Get-FileHash $png).Hash -ne
                (Get-FileHash (Join-Path $ReferenceDirectory "$package-1440/queued/$stem.png")).Hash){throw "Capture drift: $package $frame"}
        }
    }
    Test-ModeSwitch
    Write-Output "ShaderScene Pass timing, application profile and dual-package capture PASS: $out"
}finally{
    Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
