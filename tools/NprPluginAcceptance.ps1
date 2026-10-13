param([string]$BuildDirectory='build-ci-msvc',[switch]$NprOff,
    [string]$OutputDirectory='.local/verification/a4n1')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root $BuildDirectory
$exe=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release/Iris.exe'}else{Join-Path $build 'Iris.exe'}
$out=Join-Path (Join-Path $root $OutputDirectory) ('application-'+$BuildDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {
    $saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)
}
try {
    $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_ANIMATION_TIME='0'
    $env:MYRENDERER_STYLIZED=if($NprOff){'0'}else{'1'}
    $env:MYRENDERER_EDITOR_SCREENSHOT_WARMUP=if($NprOff){'4'}else{'56'}
    if(-not $NprOff){$env:IRIS_NPR_UI_TEST='1'}
    $widths=if($NprOff){@(1440)}else{@(1440,1100)}
    foreach($width in $widths) {
        $case=Join-Path $out ([string]$width)
        $env:IRIS_STATE_DIRECTORY=Join-Path $case 'state';New-Item -ItemType Directory -Force $env:IRIS_STATE_DIRECTORY | Out-Null
        Set-Content (Join-Path $env:IRIS_STATE_DIRECTORY 'language') en
        $env:IRIS_NPR_ACCEPTANCE_DIRECTORY=$case
        $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$width
        $env:MYRENDERER_EDITOR_WINDOW_HEIGHT=if($width -eq 1440){'900'}else{'680'}
        $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out ($width.ToString()+'.png')
        & $exe (Join-Path $root 'assets/scenes/fixtures/14_polyhaven_material_gallery.myscene') *> (Join-Path $out ($width.ToString()+'.log'))
        $log=Get-Content (Join-Path $out ($width.ToString()+'.log')) -Raw
        $expected=if($NprOff){'NPR OFF ordinary scene / shading command / render rejection preserved state PASS'}else{'NPR real parameter UI / both paths / legacy parity / history / failure retention / Scene / resize / replacement / disable-enable PASS'}
        if($LASTEXITCODE -ne 0 -or -not $log.Contains($expected) -or -not(Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){throw "NPR application acceptance failed: $width"}
    }
    Write-Output "NPR application UI / capability / state / lifecycle PASS: $out"
}finally {
    Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
