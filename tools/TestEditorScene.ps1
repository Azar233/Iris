param([string]$BuildDirectory = "build-ci-msvc")
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$executable = Join-Path $projectRoot "$BuildDirectory/Release/Iris.exe"
$testDirectory = Join-Path $projectRoot "$BuildDirectory/editor-regression"
New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
$previousSmoke = $env:MYRENDERER_SMOKE_TEST
$previousAppend = $env:MYRENDERER_APPEND_TEST
$previousInteraction = $env:MYRENDERER_EDITOR_INTERACTION_TEST
Push-Location $testDirectory
try {
    $env:MYRENDERER_SMOKE_TEST = '1'
    $env:MYRENDERER_EDITOR_INTERACTION_TEST = '1'
    $env:MYRENDERER_APPEND_TEST = Join-Path $projectRoot 'assets/models/sphere.obj'
    $output = & $executable (Join-Path $projectRoot 'assets/models/cube.obj') 2>&1
    $output | Write-Output
    if ($LASTEXITCODE -ne 0 -or ($output -join "`n") -notmatch 'Append scene validation: PASS') {
        throw 'Multi-model editor regression failed.'
    }
    if (($output -join "`n") -notmatch 'Editor interaction validation: PASS') {
        throw 'Editor picking / deletion / empty scene regression failed.'
    }
} finally {
    Pop-Location
    $env:MYRENDERER_SMOKE_TEST = $previousSmoke
    $env:MYRENDERER_APPEND_TEST = $previousAppend
    $env:MYRENDERER_EDITOR_INTERACTION_TEST = $previousInteraction
}
