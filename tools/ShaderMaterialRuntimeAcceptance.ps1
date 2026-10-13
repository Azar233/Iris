param([Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [string]$OutputDirectory='.local/verification/a3m2/runtime', [string]$ReferenceDirectory='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$runtime=[IO.Path]::GetFullPath((Join-Path $root $RuntimeDirectory)).TrimEnd('\','/')
$out=[IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
New-Item -ItemType Directory -Force $out | Out-Null
foreach($file in @('Iris.exe','IrisBatch.exe','MSVCP140.dll','VCRUNTIME140.dll','VCRUNTIME140_1.dll','LICENSE','THIRD_PARTY_NOTICES.md','ASSET_LICENSES.md',
    'assets/shader-materials/checker/checker.shadermaterial','assets/shader-materials/checker/surface.frag',
    'assets/shader-materials/checker/README.md','assets/shader-materials/checker/LICENSE',
    'licenses/GLFW-LICENSE.md','licenses/ASSIMP-LICENSE.txt')) {
    if(-not(Test-Path (Join-Path $runtime $file))){throw "Missing Runtime resource/license: $file"}
}
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {
    $saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)
}
Push-Location $runtime
try {
    $env:IRIS_STATE_DIRECTORY=Join-Path $out 'state'
    & (Join-Path $runtime 'Iris.exe') raster-sequence (Join-Path $runtime 'assets/renderjobs/11_shader_material.renderjob') --output (Join-Path $out 'frame_{frame:04}') *> (Join-Path $out 'raster.log')
    if($LASTEXITCODE -ne 0){throw 'Runtime material Job failed'}
    $ids=@()
    foreach($frame in @(0,1)) {
        $stem='frame_{0:d4}' -f $frame
        $png=Join-Path $out ($stem+'.png')
        if(-not(Test-Path $png)){throw "Missing frame: $stem"}
        $report=Get-Content (Join-Path $out ($stem+'-report.json')) -Raw | ConvertFrom-Json
        if($report.frame -ne $frame -or $report.fps -ne 24 -or $report.samplingSeed -ne 20261013 -or $report.renderPluginApiVersion -ne 2 -or
            [Math]::Abs($report.timeStepSeconds-1.0/24) -gt 1e-12 -or -not $report.buildId -or
            -not $report.inputManifest.complete -or $report.shaderMaterials.Count -ne 1 -or
            $report.shaderMaterials[0].id -ne 'checker_material' -or $report.inputManifest.files.Count -ne 55){throw 'Invalid material Job identity/report'}
        foreach($dll in @('MSVCP140.dll','VCRUNTIME140.dll','VCRUNTIME140_1.dll')) {
            if(-not($report.inputManifest.files | Where-Object {[IO.Path]::GetFileName($_.path) -eq $dll})){throw "CRT identity missing: $dll"}
        }
        foreach($file in $report.inputManifest.files) {
            $path=[IO.Path]::GetFullPath($file.path)
            if(-not $file.exists -or -not(Test-Path -LiteralPath $path) -or
                -not $path.StartsWith($runtime+'\',[StringComparison]::OrdinalIgnoreCase)){throw "Runtime consumed outside input: $path"}
        }
        if($ReferenceDirectory) {
            $reference=Join-Path (Join-Path $root $ReferenceDirectory) ($stem+'.png')
            if((Get-FileHash $png).Hash -ne (Get-FileHash $reference).Hash){throw 'Runtime/source fixed pixels differ'}
        }
        $ids+=$report.buildId
    }
    $job=Get-Content (Join-Path $runtime 'assets/renderjobs/11_shader_material.renderjob') -Raw | ConvertFrom-Json
    $job.renderer='cpu-path-traced';$job.scene=Join-Path $runtime 'assets/scenes/fixtures/29_shader_material.myscene'
    $job.PSObject.Properties.Remove('raster')
    $job.output.path=Join-Path $out 'cpu-rejected/frame_{frame:04}'
    $cpu=Join-Path $out 'cpu.renderjob';$job | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 $cpu
    & (Join-Path $runtime 'IrisBatch.exe') render-sequence $cpu *> (Join-Path $out 'cpu.log')
    $cpuExit=$LASTEXITCODE
    if($cpuExit -ne 70 -or (Test-Path (Join-Path $out 'cpu-rejected')) -or
        (Get-Content (Join-Path $out 'cpu.log') -Raw) -notmatch 'ShaderMaterial'){throw "CPU rejection contract failed: $cpuExit"}
    [ordered]@{runtime=$runtime;buildIds=$ids;frames=@(0,1);fps=24;filesPerManifest=55;allInputsInsideRuntime=$true;cpuRejectedExit=$cpuExit;referenceExact=[bool]$ReferenceDirectory} |
        ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $out 'acceptance.json')
    Write-Output "Independent material Runtime resources / two frames / identity / CPU rejection PASS: $out"
}finally{
    Pop-Location
    Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
