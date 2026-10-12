param([string]$BuildDirectory='build-ci-msvc', [switch]$SingleWindow)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=if([IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory}else{Join-Path $root $BuildDirectory}
$exe=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release/Iris.exe'}else{Join-Path $build 'Iris.exe'}
$out=Join-Path $root ('.local/verification/s1b4/'+(Split-Path $build -Leaf))
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -like 'MYRENDERER_*' -or $_.Name -like 'IRIS_*'} | ForEach-Object {
    $saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)
}
Push-Location $root
try {
    $env:IRIS_STATE_DIRECTORY=Join-Path $out 'state'
    New-Item -ItemType Directory -Force $env:IRIS_STATE_DIRECTORY | Out-Null
    Set-Content -LiteralPath (Join-Path $env:IRIS_STATE_DIRECTORY 'language') -Value en
    $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_ANIMATION_TIME='0.35'
    $env:MYRENDERER_RENDER_WIDTH='320';$env:MYRENDERER_RENDER_HEIGHT='180'
    $env:MYRENDERER_EDITOR_SCREENSHOT_TAB='assets';$env:MYRENDERER_EDITOR_SCREENSHOT_WARMUP='12'
    $widths=if($SingleWindow){@(1440)}else{@(1440,1100)}
    foreach($width in $widths) {
        $case=Join-Path $out ('case-'+[guid]::NewGuid().ToString('N'))
        $env:IRIS_SHADER_PACKAGE_RELOAD_ACCEPTANCE_DIRECTORY=$case
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$width
        $env:MYRENDERER_EDITOR_WINDOW_HEIGHT=if($width -eq 1440){'900'}else{'680'}
        if($width -eq 1100){$env:IRIS_SHADER_PACKAGE_RELOAD_FAILURE='1';$env:IRIS_SHADER_PACKAGE_RELOAD_UI_RETRY='1'}
        $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out "reload-$width.png"
        $log=Join-Path $out "application-$width.log"
        $ErrorActionPreference='Continue'; & $exe assets/shader-scenes/orbit/orbit.shaderscene *> $log
        $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
        if($code -ne 0 -or -not(Select-String -LiteralPath $log -SimpleMatch 'ShaderScene automatic Manifest/include/raw reload' -Quiet)) {
            throw "ShaderScene automatic reload acceptance failed: $log"
        }
        if(-not(Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){throw 'Missing editor capture'}
        if($width -eq 1100 -and -not(Select-String -LiteralPath $log -SimpleMatch 'Shader transaction retry UI interaction: PASS' -Quiet)) {
            throw 'Real package retry button did not retain history'
        }
    }
    Write-Output "ShaderScene dependency reload / automatic transactions / editor captures PASS: $out"
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object {$_.Name -like 'MYRENDERER_*' -or $_.Name -like 'IRIS_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
