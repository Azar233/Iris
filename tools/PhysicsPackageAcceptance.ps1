param([Parameter(Mandatory=$true)][string]$Zip,[Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
$zipPath=[IO.Path]::GetFullPath($Zip)
$destinationPath=[IO.Path]::GetFullPath($Destination)
if(Test-Path -LiteralPath $destinationPath){throw 'Package acceptance destination must be new'}
New-Item -ItemType Directory -Path $destinationPath | Out-Null
Expand-Archive -LiteralPath $zipPath -DestinationPath $destinationPath
if(Test-Path -LiteralPath (Join-Path $destinationPath 'Iris.exe')) {
    $packageRoot=$destinationPath
} else {
    $root=@(Get-ChildItem -LiteralPath $destinationPath -Directory | Where-Object {
        Test-Path -LiteralPath (Join-Path $_.FullName 'Iris.exe')
    })
    if($root.Count -ne 1){throw 'Cannot identify one standalone Iris package'}
    $packageRoot=$root[0].FullName
}
$receipts=Join-Path $destinationPath 'receipts'
New-Item -ItemType Directory -Path $receipts | Out-Null
foreach($file in @('Iris.exe','IrisBatch.exe','licenses/PhysX-LICENSE.md','THIRD_PARTY_NOTICES.md',
    'assets/scenes/fixtures/33_cpu_rigid_bodies.myscene','assets/scenes/fixtures/34_physics_free_fall.myscene',
    'assets/scenes/fixtures/35_physics_bounce.myscene','assets/scenes/fixtures/36_physics_stack.myscene',
    'assets/renderjobs/12_cpu_rigid_bodies.renderjob','assets/renderjobs/13_rigid_bodies_raster.renderjob',
    'assets/renderjobs/14_physics_free_fall.renderjob','assets/renderjobs/15_physics_bounce.renderjob',
    'assets/renderjobs/16_physics_stack.renderjob')) {
    if(!(Test-Path -LiteralPath (Join-Path $packageRoot $file))){throw "Package missing $file"}
}
if(@(Get-ChildItem -LiteralPath $packageRoot -Recurse -Filter 'PhysX*.dll').Count){throw 'Unexpected dynamic PhysX SDK dependency'}
Push-Location $packageRoot
try {
    foreach($name in @('14_physics_free_fall','15_physics_bounce','16_physics_stack')) {
        & ./IrisBatch.exe validate "assets/renderjobs/$name.renderjob" *> (Join-Path $receipts ($name+'-validate.log'))
        if($LASTEXITCODE -ne 0){throw "Installed $name validation failed"}
        & ./IrisBatch.exe simulate "assets/renderjobs/$name.renderjob" *> (Join-Path $receipts ($name+'-simulate.log'))
        if($LASTEXITCODE -ne 0){throw "Installed $name simulation failed"}
    }
    & ./IrisBatch.exe bake assets/renderjobs/12_cpu_rigid_bodies.renderjob *> (Join-Path $receipts 'bake.log')
    if($LASTEXITCODE -ne 0){throw 'Installed physics bake failed'}
    & ./IrisBatch.exe render-frame assets/renderjobs/12_cpu_rigid_bodies.renderjob 30 --output (Join-Path $receipts 'installed-frame') *> (Join-Path $receipts 'frame.log')
    if($LASTEXITCODE -ne 0){throw 'Installed cached frame failed'}
    $saved=Join-Path $receipts 'saved.myscene'
    & cmake -E env MYRENDERER_SMOKE_TEST=1 IRIS_PHYSICS_WORKBENCH_TEST=1 IRIS_PHYSICS_UI_TEST=module `
        ('IRIS_STATE_DIRECTORY='+$receipts+'/state') ('MYRENDERER_SCENE_ROUNDTRIP='+$saved) `
        ('MYRENDERER_EDITOR_SCREENSHOT='+$receipts+'/installed-workspace.png') `
        MYRENDERER_EDITOR_SCREENSHOT_TAB=module MYRENDERER_EDITOR_SCREENSHOT_WARMUP=36 ./Iris.exe assets/scenes/fixtures/33_cpu_rigid_bodies.myscene *> (Join-Path $receipts 'editor.log')
    if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $saved)){throw 'Installed GUI save/reopen failed'}
    $report=Get-Content -LiteralPath (Join-Path $receipts 'installed-frame-report.json') -Raw | ConvertFrom-Json
    if($report.module.physics.sdk -ne 'PhysX 5.6.1' -or $report.simulationCache.status -ne 'Hit'){
        throw 'Installed report does not identify PhysX/cache reuse'
    }
} finally {Pop-Location}
[ordered]@{zip=$zipPath;sha256=(Get-FileHash -LiteralPath $zipPath).Hash;root=$packageRoot;status='Passed';physxLinkage='static'} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $receipts 'package.json') -Encoding utf8
Write-Output "Physics standalone package: PASS ($packageRoot)"
