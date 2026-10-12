param([string]$BuildDirectory='build-ci-msvc',[switch]$SingleWindow)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root $BuildDirectory
$exe=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release/Iris.exe'}else{Join-Path $build 'Iris.exe'}
$out=Join-Path $root ('.local/verification/s1d2/workspace/'+$BuildDirectory+'/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {
    $saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)
}
try {
    $env:IRIS_STATE_DIRECTORY=Join-Path $out 'state';New-Item -ItemType Directory -Force $env:IRIS_STATE_DIRECTORY | Out-Null
    Set-Content (Join-Path $env:IRIS_STATE_DIRECTORY 'language') en
    $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_RENDER_WIDTH='320';$env:MYRENDERER_RENDER_HEIGHT='180'
    $env:MYRENDERER_EDITOR_SCREENSHOT_WARMUP='16';$env:IRIS_SHADER_DIAGNOSTIC_UI_TEST='1'
    $widths=if($SingleWindow){@(1440)}else{@(1440,1100)}
    foreach($width in $widths){
        $case=Join-Path $out "mapping-$width"
        $env:IRIS_SHADER_SOURCE_MAP_ACCEPTANCE_DIRECTORY=$case
        $env:IRIS_SHADER_BROWSER_PACKAGE=Join-Path $case 'mapping.shaderscene'
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$width
        $env:MYRENDERER_EDITOR_WINDOW_HEIGHT=if($width -eq 1440){'900'}else{'680'}
        $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out "diagnostics-$width.png"
        $info=New-Object System.Diagnostics.ProcessStartInfo
        $info.FileName=$exe;$info.Arguments='"'+(Join-Path $root 'assets/scenes/03_enscape_ocean_study.myscene')+'"'
        $info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
        $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
        $process=New-Object System.Diagnostics.Process;$process.StartInfo=$info
        if(-not $process.Start()){throw 'Cannot start workspace acceptance'}
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        try {
            if(-not $process.WaitForExit(60000)){throw 'Workspace acceptance timed out'}
            $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
            $text | Set-Content -Encoding UTF8 (Join-Path $out "diagnostics-$width.log")
            if($process.ExitCode -ne 0 -or $text -notmatch 'Original include diagnostic real selection: PASS' -or
                -not(Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){throw 'Original location/retention/UI acceptance failed'}
        }finally{$process.Dispose()}
    }
    Write-Output "ShaderScene original include diagnostics / real selection / workspace screenshots PASS: $out"
}finally{
    Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
