param([string]$BuildDirectory='build-ci-msvc',[switch]$SingleWindow,[string]$ReuseCase,[switch]$StateOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root $BuildDirectory
$exe=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release/Iris.exe'}else{Join-Path $build 'Iris.exe'}
$out=Join-Path $root ('.local/verification/s1c1/'+$BuildDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {
    $saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)
}
Push-Location $root
try {
    if($StateOnly -and -not $ReuseCase){throw 'StateOnly requires an existing acceptance case'}
    $env:IRIS_STATE_DIRECTORY=Join-Path $out ('state-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $env:IRIS_STATE_DIRECTORY | Out-Null
    Set-Content -LiteralPath (Join-Path $env:IRIS_STATE_DIRECTORY 'language') -Value en
    if(-not $ReuseCase){
    $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_ANIMATION_TIME='0.33333334'
    $env:MYRENDERER_RENDER_WIDTH='320';$env:MYRENDERER_RENDER_HEIGHT='180'
    $env:MYRENDERER_EDITOR_SCREENSHOT_TAB='render-queue'
    $widths=if($SingleWindow){@(1440)}else{@(1440,1100)}
    foreach($width in $widths){
        $case=Join-Path $out ('case-'+[guid]::NewGuid().ToString('N'))
        $env:IRIS_SHADER_SEQUENCE_ACCEPTANCE_DIRECTORY=$case
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$width
        $env:MYRENDERER_EDITOR_WINDOW_HEIGHT=if($width -eq 1440){'900'}else{'680'}
        $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out "queue-$width.png"
        $log=Join-Path $out "gui-$width.log"
        $ErrorActionPreference='Continue'; & $exe assets/shader-scenes/sequence/sequence.shaderscene *> $log
        $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
        if($code -ne 0 -or -not(Select-String -LiteralPath $log -SimpleMatch 'six-frame PNG parity / safe cancellation: PASS' -Quiet) -or
            -not(Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){throw "GUI / Queue failed ($code): $log"}
    }
    Get-ChildItem Env: | Where-Object {$_.Name -like 'MYRENDERER_*' -or ($_.Name -like 'IRIS_*' -and $_.Name -ne 'IRIS_STATE_DIRECTORY')} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    $jobPath=Join-Path $case 'sequence.renderjob'
    foreach($run in 'cli-one','cli-two'){
        $log=Join-Path $out ($run+'.log')
        $ErrorActionPreference='Continue'; & $exe raster-sequence $jobPath --output (Join-Path $case "$run/frame_{frame:04}") *> $log
        $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
        if($code -ne 0){throw "CLI sequence failed ($code): $log"}
        foreach($frame in 3..8){
            $name='frame_{0:0000}' -f $frame
            foreach($suffix in '.png','-report.json'){
                $expected=(Get-FileHash (Join-Path $case "queued/$name$suffix")).Hash
                if((Get-FileHash (Join-Path $case "$run/$name$suffix")).Hash -ne $expected){throw "Queue/CLI mismatch: $frame$suffix"}
            }
        }
    }
    $report=Get-Content (Join-Path $case 'cli-one/frame_0003-report.json') -Raw | ConvertFrom-Json
    if(-not $report.inputManifest.complete -or $report.shaderScene.sampleIndex -ne 3 -or $report.shaderScene.seed -ne 7 -or
        $report.shaderScene.warmupFrames -ne 3 -or $report.capture.historyResetPerOutput -or
        $report.timeSeconds -ne 0.125 -or -not($report.inputManifest.files.path -match 'sequence-common.glsl')) {throw 'Sequence report lacks effective inputs'}
    $base=Get-Content $jobPath -Raw
    $unicode=Join-Path $case ([string][char]0x4E2D+[string][char]0x6587+'-output')
    $ErrorActionPreference='Continue'; & $exe raster-sequence $jobPath --output (Join-Path $unicode 'frame_{frame:04}') *> (Join-Path $out 'unicode-output.log')
    $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
    if($code -ne 0 -or (Get-FileHash (Join-Path $unicode 'frame_0003.png')).Hash -ne (Get-FileHash (Join-Path $case 'queued/frame_0003.png')).Hash){throw 'Unicode Raster output failed'}
    foreach($mode in 'legacy','hdr','resume','low-fps','native','bad-compile'){
        $job=$base | ConvertFrom-Json
        $rejected=Join-Path $case "rejected-$mode"
        $job.scene=Join-Path $case 'saved.myscene';$job.output.path=Join-Path $rejected 'frame_{frame:04}'
        $expected=65
        switch($mode){
            legacy {$job.schemaVersion=3;$job.PSObject.Properties.Remove('shaderScene');$expected=66}
            hdr {$job.output.formats=@('hdr')}
            resume {$job.output.resume=$true}
            low-fps {$job.frames.fps=1;$expected=66}
            native {$job.scene=Join-Path $root 'assets/scenes/fixtures/27_cloud_lab_regression.myscene';$expected=66}
            bad-compile {
                $package=Join-Path $case 'bad-package';New-Item -ItemType Directory -Force $package | Out-Null
                Copy-Item -LiteralPath (Join-Path $root 'assets/shader-scenes/sequence') -Destination (Join-Path $package 'source') -Recurse
                Set-Content -LiteralPath (Join-Path $package 'source/accum.frag') -Value '#version 330 core invalid'
                $scene=Get-Content (Join-Path $case 'saved.myscene') -Raw | ConvertFrom-Json
                $scene.renderer.shaderSceneResource=Join-Path $package 'source/sequence.shaderscene'
                $job.scene=Join-Path $package 'bad.myscene';$scene | ConvertTo-Json -Depth 64 | Set-Content -Encoding UTF8 $job.scene
                $expected=66
            }
        }
        $path=Join-Path $case "$mode.renderjob";$job | ConvertTo-Json -Depth 64 | Set-Content -Encoding UTF8 $path
        $ErrorActionPreference='Continue'; & $exe raster-sequence $path *> (Join-Path $out "$mode-rejected.log")
        $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
        if($code -ne $expected -or (Test-Path $rejected)){throw "Unsupported/invalid request produced output: $mode ($code)"}
    }
    # A collision in a later frame must reject the whole job before frame 3 exists.
    $collision=Join-Path $case 'collision';New-Item -ItemType Directory -Force $collision | Out-Null
    Set-Content -LiteralPath (Join-Path $collision 'frame_0008.png') -Value sentinel
    $ErrorActionPreference='Continue'; & $exe raster-sequence $jobPath --output (Join-Path $collision 'frame_{frame:04}') *> (Join-Path $out 'collision.log')
    $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
    if($code -ne 73 -or (Test-Path (Join-Path $collision 'frame_0003.png'))){throw 'Later-frame collision was not preflighted'}
    }else{
        $case=[IO.Path]::GetFullPath($ReuseCase)
        $jobPath=Join-Path $case 'sequence.renderjob';$base=Get-Content $jobPath -Raw
    }
    if($StateOnly){
        $recent=Join-Path $env:IRIS_STATE_DIRECTORY 'recent-scene'
        Set-Content -LiteralPath $recent -Value 'iris-sequence-state-sentinel'
        $queueState=Join-Path $env:IRIS_STATE_DIRECTORY 'render-queue.json'
        Set-Content -LiteralPath $queueState -Value 'iris-queue-state-sentinel'
        $recentHash=(Get-FileHash $recent).Hash;$queueHash=(Get-FileHash $queueState).Hash
        $stateOutput=Join-Path $case ('state-only-'+[guid]::NewGuid().ToString('N'))
        $ErrorActionPreference='Continue'; & $exe raster-sequence $jobPath --output (Join-Path $stateOutput 'frame_{frame:04}') *> (Join-Path $out 'state-isolation.log')
        $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
        if($code -ne 0 -or (Get-FileHash $recent).Hash -ne $recentHash -or (Get-FileHash $queueState).Hash -ne $queueHash){throw 'Raster worker modified editor state'}
        foreach($frame in 3..8){$name='frame_{0:0000}.png' -f $frame
            if((Get-FileHash (Join-Path $stateOutput $name)).Hash -ne (Get-FileHash (Join-Path $case "queued/$name")).Hash){throw 'State isolation changed sequence pixels'}}
        Write-Output "Raster worker recent-scene / Queue state isolation and six-frame PNG parity PASS: $out"
        return
    }
    # Mutate an isolated include after publication begins; earlier complete pairs
    # survive, while no frame using changed inputs or staging file is published.
    $mutation=Join-Path $case ('mutation-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $mutation | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'assets/shader-scenes/sequence') -Destination (Join-Path $mutation 'source') -Recurse
    $scene=Get-Content (Join-Path $case 'saved.myscene') -Raw | ConvertFrom-Json
    $scene.renderer.shaderSceneResource=Join-Path $mutation 'source/sequence.shaderscene'
    $scenePath=Join-Path $mutation 'scene.myscene';$scene | ConvertTo-Json -Depth 64 | Set-Content -Encoding UTF8 $scenePath
    $job=$base | ConvertFrom-Json;$job.scene=$scenePath;$job.frames.start=0;$job.frames.end=999;$job.shaderScene.warmupFrames=0
    $job.output.path=Join-Path $mutation 'output/frame_{frame:04}'
    $mutationJob=Join-Path $mutation 'input.renderjob';$job | ConvertTo-Json -Depth 64 | Set-Content -Encoding UTF8 $mutationJob
    $info=New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName=$exe;$info.Arguments='raster-sequence "'+$mutationJob+'"'
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WorkingDirectory=$root
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $process=New-Object System.Diagnostics.Process;$process.StartInfo=$info
    if(-not $process.Start()){throw 'Cannot launch mutation fixture'}
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    while(-not(Test-Path (Join-Path $mutation 'output/frame_0000-report.json')) -and -not $process.HasExited -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 5;$process.Refresh()}
    if($process.HasExited -or [DateTime]::UtcNow -ge $deadline){throw 'Mutation fixture did not start publishing'}
    Add-Content -LiteralPath (Join-Path $mutation 'source/sequence-common.glsl') -Value '// changed during capture'
    if(-not $process.WaitForExit(30000)){throw 'Input mutation did not stop Raster capture'}
    $stdout.GetAwaiter().GetResult() | Set-Content -Encoding UTF8 (Join-Path $out 'mutation.log')
    $stderr.GetAwaiter().GetResult() | Set-Content -Encoding UTF8 (Join-Path $out 'mutation-error.log')
    $diagnostic=if($process.ExitCode -eq 1){'Fatal error: Capture input changed'}elseif($process.ExitCode -eq 74){'Raster report failed: Capture input changed'}else{''}
    if(-not $diagnostic -or -not(Select-String -LiteralPath (Join-Path $out 'mutation-error.log') -SimpleMatch $diagnostic -Quiet)){throw "Capture did not reject changed inputs (exit $($process.ExitCode))"}
    $pairs=@(Get-ChildItem (Join-Path $mutation 'output') -Filter '*-report.json')
    if($pairs.Count -lt 1 -or $pairs.Count -ge 1000 -or @(Get-ChildItem (Join-Path $mutation 'output') -Filter '*partial*').Count -ne 0){throw 'Mutation leaked staging output or completed all frames'}
    foreach($pair in $pairs){if(-not(Test-Path ($pair.FullName.Replace('-report.json','.png')))){throw 'Mutation left a report without PNG'}}
    $process.Dispose()
    if($ReuseCase){Write-Output "ShaderScene input mutation / preserved pairs / no staging leak PASS: $out"}
    else{Write-Output "ShaderScene sequence GUI / Queue cancellation / CLI repeat / report identities / rejection PASS: $out"}
}finally{
    Pop-Location
    Get-ChildItem Env: | Where-Object {$_.Name -like 'MYRENDERER_*' -or $_.Name -like 'IRIS_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
