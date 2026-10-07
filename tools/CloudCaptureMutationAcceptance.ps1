param([Parameter(Mandatory=$true)][string]$Renderer,
      [Parameter(Mandatory=$true)][string]$SourceRoot,
      [Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $taskRoot -Force | Out-Null
$sourceModel = Join-Path $SourceRoot 'assets/models/cube.obj'
$model = Join-Path $taskRoot 'mutable.obj'
$before = (Get-FileHash -LiteralPath $sourceModel -Algorithm SHA256).Hash
Copy-Item -LiteralPath $sourceModel -Destination $model -Force
$scene = Get-Content -LiteralPath (Join-Path $SourceRoot 'assets/scenes/fixtures/27_cloud_lab_regression.myscene') -Raw | ConvertFrom-Json
foreach ($entity in $scene.entities) {
    if (-not $entity.model.StartsWith('builtin:')) { $entity.model = $model.Replace('\','/') }
}
$scenePath = Join-Path $taskRoot 'mutation.myscene'
[IO.File]::WriteAllText($scenePath, ($scene | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
$job = Get-Content -LiteralPath (Join-Path $SourceRoot 'assets/renderjobs/05_cloud_determinism.renderjob') -Raw | ConvertFrom-Json
$job.scene = $scenePath.Replace('\','/')
$job.resolution = @(1280,720)
$job.raster.warmupFrames = 10
$job.output.path = (Join-Path $taskRoot 'frame_{frame:04}').Replace('\','/')
$jobPath = Join-Path $taskRoot 'mutation.renderjob'
[IO.File]::WriteAllText($jobPath, ($job | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
foreach ($i in 0..2) {
    $stem = Join-Path $taskRoot ('frame_{0:0000}' -f $i)
    foreach ($suffix in @('.png','-report.json','.png.partial.png','-report.json.partial')) {
        $file = $stem + $suffix
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
}
$stdout = Join-Path $taskRoot 'stdout.log'
$stderr = Join-Path $taskRoot 'stderr.log'
$child = Start-Process -FilePath $Renderer -ArgumentList @('raster-sequence', ('"' + $jobPath + '"')) -WorkingDirectory $SourceRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
try {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $first = Join-Path $taskRoot 'frame_0000-report.json'
    while (-not (Test-Path -LiteralPath $first)) {
        if ($child.HasExited) { throw 'Capture exited before mutation trigger' }
        if ($watch.Elapsed.TotalSeconds -gt 60) { throw 'Timed out waiting for first frame' }
        Start-Sleep -Milliseconds 20
    }
    # Change only the test copy after the renderer has consumed it and published frame zero.
    [IO.File]::AppendAllText($model, "`n# changed during capture`n")
    if (-not $child.WaitForExit(30000)) { throw 'Capture did not stop after dependency mutation' }
    $child.Refresh()
    $errors = Get-Content -LiteralPath $stderr -Raw
    if ($child.ExitCode -eq 0 -or $errors -notmatch 'Capture input changed') { throw "Mutation was not rejected: $errors" }
    if (Test-Path -LiteralPath (Join-Path $taskRoot 'frame_0002-report.json')) { throw 'A later frame was published with changed inputs' }
    if ((Get-FileHash -LiteralPath $sourceModel -Algorithm SHA256).Hash -ne $before) { throw 'Source asset changed' }
    Write-Output 'Capture mutation acceptance: PASS (changed dependency rejected, original asset preserved)'
} finally {
    if (-not $child.HasExited) { $child.Kill(); $child.WaitForExit() }
}
