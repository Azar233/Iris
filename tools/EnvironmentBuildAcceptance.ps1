param([string]$BuildDirectory='build-ci-msvc')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "$BuildDirectory/Release/Iris.exe"
$output=Join-Path $root "$BuildDirectory/environment-parallel/$([guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Force $output | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object { $saved[$_.Name]=$_.Value; Remove-Item -LiteralPath ('Env:'+$_.Name) }
Push-Location $root
try {
    $timings=@{}
    foreach($run in 'serial','parallel') {
        $env:MYRENDERER_ENVIRONMENT_WORKERS=if($run -eq 'serial'){'1'}else{'8'}
        & $exe raster-sequence assets/renderjobs/06_ocean_clouds_hero.renderjob --output (Join-Path $output "$run/frame_{frame:04}") *> (Join-Path $output "$run.log")
        if($LASTEXITCODE -ne 0){throw "$run sequence failed"}
        $frames=@(Get-ChildItem (Join-Path $output $run) -Filter 'frame_????.png')
        $reports=@(Get-ChildItem (Join-Path $output $run) -Filter '*-report.json')
        if($frames.Count -ne 24 -or $reports.Count -ne 24){throw "$run sequence incomplete"}
        foreach($report in $reports) {
            $json=Get-Content $report.FullName -Raw | ConvertFrom-Json
            if(-not $json.inputManifest.complete -or $json.moduleSeed -ne 20261006 -or -not $json.water.surfaceOptics){throw "$run invalid inputs"}
        }
        $samples=@([regex]::Matches((Get-Content (Join-Path $output "$run.log") -Raw),'Atmosphere environment rebuilt in ([0-9.]+) ms') | ForEach-Object { [double]::Parse($_.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture) } | Sort-Object)
        if($samples.Count -ne 24){throw "$run expected 24 measured sun rebuilds"}
        $timings[$run]=[pscustomobject]@{Samples=24;P50Ms=$samples[11];P95Ms=$samples[22]}
    }
    foreach($frame in Get-ChildItem (Join-Path $output 'serial') -Filter 'frame_????.png') {
        if((Get-FileHash $frame.FullName).Hash -ne (Get-FileHash (Join-Path $output ('parallel/'+$frame.Name))).Hash){throw "Parallel image mismatch: $($frame.Name)"}
    }
    if($timings.parallel.P50Ms -ge $timings.serial.P50Ms * 0.8){throw 'Parallel median must improve by at least 20 percent on the acceptance machine'}
    $summary=[pscustomobject]@{Directory=$output;IdenticalFrames=24;Reports=48;Serial=$timings.serial;Parallel=$timings.parallel;Speedup=$timings.serial.P50Ms/$timings.parallel.P50Ms}
    $summary | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $output 'summary.json')
    $summary | ConvertTo-Json -Depth 4 | Set-Content (Join-Path (Split-Path $output -Parent) 'summary.json')
    $summary | ConvertTo-Json -Depth 4
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object Name -Like 'MYRENDERER_*' | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($name in $saved.Keys){Set-Item -LiteralPath ('Env:'+$name) -Value $saved[$name]}
}
Write-Output 'Environment build acceptance: PASS'
