param([string]$BuildDirectory='build-ci-msvc',[switch]$WaterOff,
    [string]$OutputDirectory='.local/verification/a4w1')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root $BuildDirectory
$exe=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release/Iris.exe'}else{Join-Path $build 'Iris.exe'}
$out=Join-Path (Join-Path $root $OutputDirectory) ('jobs-'+$BuildDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$reference=Join-Path (Join-Path $root $OutputDirectory) ('application-'+$(if($WaterOff){'build-ci-msvc'}else{$BuildDirectory})+'/1100')
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {
    $saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)
}
try {
    $env:IRIS_STATE_DIRECTORY=Join-Path $out 'state'
    foreach($path in @(0,1)) {
        $case=Join-Path $out ([string]$path);New-Item -ItemType Directory -Force $case | Out-Null
        $scene=Get-Content (Join-Path $reference 'saved.myscene') -Raw | ConvertFrom-Json
        # The saved GUI Scene uses relative model paths; resolve before relocating.
        foreach($entity in $scene.entities) {
            if($entity.model -and $entity.model -notlike 'builtin:*' -and -not [IO.Path]::IsPathRooted($entity.model)) {
                $entity.model=[IO.Path]::GetFullPath((Join-Path $reference $entity.model))
            }
        }
        $scene.renderer.renderPath=$path
        $scenePath=Join-Path $case 'water.myscene';$scene | ConvertTo-Json -Depth 30 | Set-Content -Encoding UTF8 $scenePath
        $job=Get-Content (Join-Path $root 'assets/renderjobs/11_shader_material.renderjob') -Raw | ConvertFrom-Json
        $job.scene=$scenePath;$job.output.path=Join-Path $case 'frame_{frame:04}'
        $jobPath=Join-Path $case 'water.renderjob';$job | ConvertTo-Json -Depth 15 | Set-Content -Encoding UTF8 $jobPath
        # Windows PowerShell surfaces native stderr as NativeCommandError;
        # the OFF case intentionally returns 66 and must reach its assertions.
        if($WaterOff){$ErrorActionPreference='Continue'}
        try { & $exe raster-sequence $jobPath *> (Join-Path $case 'raster.log');$exit=$LASTEXITCODE }
        finally { $ErrorActionPreference='Stop' }
        if($WaterOff) {
            if($exit -ne 66 -or @(Get-ChildItem -LiteralPath $case -File -Filter 'frame_*').Count -ne 0 -or
                (Get-Content (Join-Path $case 'raster.log') -Raw) -notmatch 'iris.water'){throw 'Missing Water Job silently fell back or wrote output'}
        }else {
            if($exit -ne 0){throw 'Water Raster Job failed'}
            foreach($frame in @(0,1)) {
                $stem='frame_{0:d4}' -f $frame
                $report=Get-Content (Join-Path $case ($stem+'-report.json')) -Raw | ConvertFrom-Json
                if(-not $report.inputManifest.complete -or -not $report.buildId -or $report.renderPluginApiVersion -ne 2 -or
                    $report.frame -ne $frame -or $report.fps -ne 24 -or $report.samplingSeed -ne 20261013 -or
                    [Math]::Abs($report.timeStepSeconds-1.0/24) -gt 1e-12 -or -not(Test-Path (Join-Path $case ($stem+'.png')))){throw 'Incomplete Water Job report/output'}
            }
            if($path -eq 0 -and (Get-FileHash (Join-Path $case 'frame_0000.png')).Hash -ne
                (Get-FileHash (Join-Path $reference 'edited.png')).Hash){throw 'Water GUI/Job fixed pixels differ'}
        }
    }
    Write-Output "Water Job both paths / GUI parity or missing-capability refusal PASS: $out"
}finally {
    Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
