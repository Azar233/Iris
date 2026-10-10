param([string]$BuildDirectory='build-ci-msvc')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=if([IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory}else{Join-Path $root $BuildDirectory}
$bin=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release'}else{$build}
$exe=Join-Path $bin 'Iris.exe'
$out=Join-Path $build 'shader-transactions'
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
function Get-TransactionHash([string]$Path) {
    $digest=[Security.Cryptography.SHA256]::Create()
    $stream=[IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($digest.ComputeHash($stream))).Replace('-','') }
    finally { $stream.Dispose(); $digest.Dispose() }
}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {$saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)}
Push-Location $root
try {
    $env:MYRENDERER_SMOKE_TEST='1'
    $env:MYRENDERER_EDITOR_SCREENSHOT_TAB='renderer'
    foreach($size in @(@(1440,900),@(1100,680))) {
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$size[0]
        $env:MYRENDERER_EDITOR_WINDOW_HEIGHT=[string]$size[1]
        foreach($mode in @('failure','recovery')) {
            $case=Join-Path $out "$mode-$($size[0])x$($size[1])"
            $env:MYRENDERER_SHADER_TRANSACTION_DIRECTORY=$case
            $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $case 'editor.png'
            if($mode -eq 'failure'){$env:MYRENDERER_SHADER_TRANSACTION_UI_RETRY='1'}
            else{Remove-Item Env:MYRENDERER_SHADER_TRANSACTION_UI_RETRY -ErrorAction SilentlyContinue}
            $log=Join-Path $out "$mode-$($size[0]).log"
            & $exe assets/models/cube.obj *> $log
            if($LASTEXITCODE -ne 0 -or -not(Select-String -LiteralPath $log -SimpleMatch 'recovery / restoration: PASS' -Quiet)){
                throw "Shader transaction application acceptance failed: $mode/$($size[0])"
            }
            if($mode -eq 'failure' -and -not(Select-String -LiteralPath $log -SimpleMatch 'retry UI interaction: PASS' -Quiet)){
                throw 'Actual retry button interaction did not complete'
            }
            foreach($file in @('before.png','retained.png','recovered.png','editor.png')){
                if(-not(Test-Path (Join-Path $case $file))){throw "Missing Shader transaction evidence: $file"}
            }
            $before=Get-TransactionHash (Join-Path $case 'before.png')
            $retained=Get-TransactionHash (Join-Path $case 'retained.png')
            $recovered=Get-TransactionHash (Join-Path $case 'recovered.png')
            if($before -ne $retained -or $before -eq $recovered){throw 'Rejected/committed program pixels failed their assertions'}
        }
    }
    Write-Output "Shader transaction GPU / cross-plugin retention / history / recovery / retry UI / double-size captures PASS: $out"
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
