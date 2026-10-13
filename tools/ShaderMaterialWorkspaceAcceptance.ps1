param([string]$BuildDirectory='build-ci-msvc',[string]$RuntimeDirectory='',
    [string]$OutputDirectory='.local/verification/a3m2/workspace')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$source=if($RuntimeDirectory){[IO.Path]::GetFullPath((Join-Path $root $RuntimeDirectory))}else{$root}
$build=Join-Path $root $BuildDirectory
$exe=if($RuntimeDirectory){Join-Path $source 'Iris.exe'}elseif(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release/Iris.exe'}else{Join-Path $build 'Iris.exe'}
$out=[IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {
    $saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)
}
try {
    $env:IRIS_STATE_DIRECTORY=Join-Path $out 'state';New-Item -ItemType Directory -Force $env:IRIS_STATE_DIRECTORY | Out-Null
    Set-Content (Join-Path $env:IRIS_STATE_DIRECTORY 'language') en
    $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_EDITOR_SCREENSHOT_WARMUP='42'
    foreach($case in @('1440','1100','no-target')) {
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=if($case -eq '1440'){'1440'}else{'1100'}
        $env:MYRENDERER_EDITOR_WINDOW_HEIGHT=if($case -eq '1440'){'900'}else{'680'}
        $env:IRIS_SHADER_MATERIAL_WORKSPACE_DIRECTORY=Join-Path $out $case
        $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out ($case+'.png')
        if($case -eq 'no-target'){$env:IRIS_SHADER_MATERIAL_WORKSPACE_NO_TARGET='1'}else{Remove-Item Env:IRIS_SHADER_MATERIAL_WORKSPACE_NO_TARGET -ErrorAction SilentlyContinue}
        $info=New-Object System.Diagnostics.ProcessStartInfo
        $info.FileName=$exe;$info.Arguments='"'+(Join-Path $source 'assets/scenes/fixtures/29_shader_material.myscene')+'"'
        $info.WorkingDirectory=$source;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
        $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
        $process=New-Object System.Diagnostics.Process;$process.StartInfo=$info
        try {
            if(-not $process.Start()){throw 'Cannot start material workspace acceptance'}
            $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
            if(-not $process.WaitForExit(60000)){$process.Kill();throw 'Material workspace acceptance timed out'}
            $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
            $text | Set-Content -Encoding UTF8 (Join-Path $out ($case+'.log'))
            if($process.ExitCode -ne 0 -or $text -notmatch 'Material workspace real bind/recompile/include navigation/repair: PASS' -or
                -not(Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){throw "Material workspace acceptance failed: $case"}
            $expected=Join-Path $env:IRIS_SHADER_MATERIAL_WORKSPACE_DIRECTORY $(if($case -eq 'no-target'){'native.png'}else{'expected.png'})
            $result=Join-Path $env:IRIS_SHADER_MATERIAL_WORKSPACE_DIRECTORY 'result.png'
            if((Get-FileHash $expected).Hash -ne (Get-FileHash $result).Hash){throw "Fixed material pixels changed: $case"}
        }finally{$process.Dispose()}
    }
    Write-Output "Material workspace bind / include navigation / repair / disabled target / fixed pixels PASS: $out"
}finally{
    Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
