param(
    [string]$BuildDirectory='build-ci-msvc',
    [string]$RenderDocCommand='output/renderdoc/portable/RenderDoc_1.46_64/renderdoccmd.exe'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=if([IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory}else{Join-Path $root $BuildDirectory}
$command=if([IO.Path]::IsPathRooted($RenderDocCommand)){$RenderDocCommand}else{Join-Path $root $RenderDocCommand}
if(-not(Test-Path -LiteralPath $command)){throw 'RenderDoc CLI is required; pass -RenderDocCommand'}
$exe=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release/Iris.exe'}else{Join-Path $build 'Iris.exe'}
$out=Join-Path $build 'a2-frame-capture'
New-Item -ItemType Directory -Force $out | Out-Null
$prefix=Join-Path $out ('frame-'+[guid]::NewGuid().ToString('N'))
$saved=@{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {$saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)}
Push-Location $root
try {
    $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_RENDERDOC_CAPTURE='1'
    $env:MYRENDERER_EDITOR_WINDOW_WIDTH='1440';$env:MYRENDERER_EDITOR_WINDOW_HEIGHT='900'
    $ErrorActionPreference='Continue'
    & $command capture -w -d $root -c $prefix $exe assets/models/cube.obj *> (Join-Path $out 'capture.log')
    $captureExit=$LASTEXITCODE;$ErrorActionPreference='Stop'
    if($captureExit -ne 0){throw 'RenderDoc launch failed'}
    $captures=@(Get-ChildItem -LiteralPath $out -Filter ((Split-Path $prefix -Leaf)+'*.rdc'))
    if($captures.Count -lt 1){throw 'No real GPU frame capture was produced'}
    $capture=$captures[0].FullName
    $xml=$capture+'.xml'
    $ErrorActionPreference='Continue'
    & $command convert -f $capture -o $xml -c xml *> (Join-Path $out 'convert.log')
    $convertExit=$LASTEXITCODE;$ErrorActionPreference='Stop'
    if($convertExit -ne 0 -or -not(Test-Path -LiteralPath $xml)){throw 'Capture conversion failed'}
    if(-not(Select-String -LiteralPath $xml -Pattern 'World axes overlay|Tone map and bloom|Sky background|Opaque forward' -Quiet)){
        throw 'Capture did not contain renderer debug-group labels'
    }
    @{capture=$capture;structured=$xml;labelsVerified=$true;fixture='assets/models/cube.obj'} |
        ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $out 'report.json')
    Write-Output "Real RenderDoc frame / debug-group labels PASS: $capture"
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
