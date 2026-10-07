param([Parameter(Mandatory=$true)][string]$PackageRoot,
      [Parameter(Mandatory=$true)][string]$SourceSequence)
$ErrorActionPreference='Stop'
$package=(Resolve-Path -LiteralPath $PackageRoot).Path
$proof=Join-Path $package 'acceptance'
New-Item -ItemType Directory -Force -Path $proof | Out-Null
foreach($file in @('Iris.exe','IrisBatch.exe','msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll',
    'README.md','LICENSE','THIRD_PARTY_NOTICES.md','ASSET_LICENSES.md','src/optics/CloudField.h',
    'shaders/water_medium.glsl','shaders/skybox.frag','assets/scenes/01_ocean_clouds_hero.myscene',
    'assets/scenes/03_enscape_ocean_study.myscene','assets/renderjobs/06_ocean_clouds_hero.renderjob',
    'assets/readme/m2d-hero.png','assets/readme/m2d-native-reel.gif',
    'shaders/third_party/enscape_cube/LICENSE.md')) {
    if(-not(Test-Path -LiteralPath (Join-Path $package $file))){throw "Missing package file: $file"}
}
if((Get-ChildItem (Join-Path $package 'licenses') -File).Count -lt 7){throw 'Incomplete dependency licenses'}
foreach($old in '01_volumetric_cloud_lab.myscene','02_ocean_weather_hero.myscene') {
    if(Test-Path -LiteralPath (Join-Path $package ('assets/scenes/'+$old))){throw 'Removed scene remains in package'}
}
$saved=@{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {$saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)}
$oldPath=$env:PATH
Push-Location $package
try {
    $env:PATH="$package;$env:SystemRoot/System32;$env:SystemRoot"
    $env:MYRENDERER_RENDER_QUEUE_STATE=Join-Path $proof 'queue.json'
    foreach($run in 'first','repeat') {
        & (Join-Path $package 'Iris.exe') raster-sequence assets/renderjobs/06_ocean_clouds_hero.renderjob --output (Join-Path $proof "$run/frame_{frame:04}") *> (Join-Path $proof "$run.log")
        if($LASTEXITCODE -ne 0){throw "Relocated sequence failed: $run"}
        $frames=@(Get-ChildItem (Join-Path $proof $run) -Filter 'frame_????.png')
        $reports=@(Get-ChildItem (Join-Path $proof $run) -Filter '*-report.json')
        if($frames.Count -ne 24 -or $reports.Count -ne 24){throw 'Incomplete relocated outputs'}
        foreach($reportFile in $reports) {
            $report=Get-Content $reportFile.FullName -Raw | ConvertFrom-Json
            if(-not $report.inputManifest.complete -or -not $report.water.surfaceOptics -or $report.moduleSeed -ne 20261006){throw 'Invalid package frame input'}
            foreach($inputFile in $report.inputManifest.files) {
                $path=[IO.Path]::GetFullPath($inputFile.path)
                if(-not $path.StartsWith($package+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -and
                    -not $path.StartsWith($env:SystemRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
                    throw "Runtime reads outside relocated package: $path"
                }
            }
        }
    }
    foreach($frame in Get-ChildItem (Join-Path $proof 'first') -Filter 'frame_????.png') {
        $hash=(Get-FileHash $frame.FullName).Hash
        if($hash -ne (Get-FileHash (Join-Path $proof ('repeat/'+$frame.Name))).Hash -or
           $hash -ne (Get-FileHash (Join-Path $SourceSequence $frame.Name)).Hash){throw "Relocated pixels differ: $($frame.Name)"}
    }
    & (Join-Path $package 'IrisBatch.exe') render-sequence assets/renderjobs/01_cpu_reference.renderjob --output (Join-Path $proof 'cpu/frame_{frame:04}') *> (Join-Path $proof 'cpu.log')
    if($LASTEXITCODE -ne 0){throw 'Relocated CPU reference Job failed'}
    $env:MYRENDERER_SMOKE_TEST='1'
    $env:MYRENDERER_ANIMATION_TIME='1.25'
    $env:MYRENDERER_SCREENSHOT=Join-Path $proof 'enscape-study.png'
    & (Join-Path $package 'Iris.exe') assets/scenes/03_enscape_ocean_study.myscene *> (Join-Path $proof 'enscape.log')
    if($LASTEXITCODE -ne 0 -or -not(Test-Path $env:MYRENDERER_SCREENSHOT)){throw 'Relocated GLSL study failed'}
    [pscustomobject]@{Package=$package;Frames=24;RepeatedIdenticalFrames=24;SourceIdenticalFrames=24;Reports=48;
        RuntimeManifest='Package/System only';DeveloperPathRemoved=$true;CpuReference='PASS';EnscapeStudy='PASS';NetworkDownloads='None requested'} |
        ConvertTo-Json | Set-Content (Join-Path $proof 'summary.json')
} finally {
    Pop-Location;$env:PATH=$oldPath
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
Write-Output 'M2-D independent package: PASS'
