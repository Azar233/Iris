param(
    [ValidateSet('msvc','gcc','plugins-off')][string]$Compiler='msvc',
    [ValidateSet('core','state','scene','assets','jobs','plugins','shader-scene','path-tracing','optics','physics')][string]$Area='core',
    [ValidateSet('Focused','Integration','Release')][string]$Level='Focused',
    [string[]]$GpuTargets=@(),
    [int]$Jobs=8,
    [switch]$Configure,
    [switch]$PlanOnly
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$buildName=@{msvc='build-ci-msvc';gcc='build-mingw';'plugins-off'='build-stored-off'}[$Compiler]
$build=Join-Path $root $buildName
$config='Release'
$out=Join-Path $root ('.local/verification/'+$Compiler+'-'+$Area+'-'+$Level.ToLower())
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $root
try {
    $indexPath=Join-Path $build 'iris-test-targets.json'
    $generationStamp=Join-Path $build 'CMakeFiles/generate.stamp'
    $staleIndex= -not(Test-Path $indexPath) -or -not(Test-Path $generationStamp)
    if(-not $staleIndex){
        # file(GENERATE) does not rewrite an unchanged owner index on every
        # configure; CMake's generation stamp records the successful refresh.
        $indexTime=(Get-Item -LiteralPath $generationStamp).LastWriteTimeUtc
        foreach($registrationFile in @('CMakeLists.txt','CMakePresets.json','cmake/IrisVerification.cmake')){
            if((Get-Item -LiteralPath (Join-Path $root $registrationFile)).LastWriteTimeUtc -gt $indexTime){$staleIndex=$true;break}
        }
    }
    # Discover tests only after the registration inputs match this build tree.
    # Otherwise an automatic configure during build can add unbuilt test owners.
    if($Configure -or $staleIndex) {
        if($PlanOnly){throw 'Configure this preset before asking for an executable plan'}
        & cmake --preset $Compiler *> (Join-Path $out 'configure.log')
        if($LASTEXITCODE -ne 0){throw "Configure failed: $out"}
    }
    $query=@('--test-dir',$build,'-C',$config,'--show-only=json-v1')
    if($Level -eq 'Focused'){$query+=@('-L',('^'+$Area+'$'))}
    $testJson=& ctest @query
    if($LASTEXITCODE -ne 0){throw 'Could not inspect CTest plan'}
    $tests=($testJson -join "`n" | ConvertFrom-Json).tests
    if(@($tests).Count -eq 0){throw "No tests selected for $Area"}
    $owners=Get-Content (Join-Path $build 'iris-test-targets.json') -Raw | ConvertFrom-Json
    $targets=@(foreach($test in $tests) {
        $entry=$owners.PSObject.Properties[$test.name]
        if(-not $entry -or -not @($entry.Value | Where-Object {$_}).Count){throw "Missing target declaration for $($test.name)"}
        $entry.Value
    }) | Sort-Object -Unique
    if($targets.Count -eq 0){throw 'Selected tests have no owning executable targets'}
    $plan=[ordered]@{compiler=$Compiler;level=$Level;area=$Area;build=$buildName;targets=$targets;tests=@($tests.name);gpu=$GpuTargets;package=($Level -eq 'Release')}
    $plan | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $out 'plan.json')
    if($PlanOnly){$plan | ConvertTo-Json -Depth 8; return}
    $buildTargets=@($targets)
    if($Level -eq 'Release'){$buildTargets+=@('MyRenderer','MyRendererBatch')}
    $buildTargets=@($buildTargets | Sort-Object -Unique)
    & cmake --build $build --config $config --target $buildTargets --parallel $Jobs *> (Join-Path $out 'build.log')
    if($LASTEXITCODE -ne 0){throw "Build failed: $out"}
    # Each GPU target finishes before another build/copy or GPU run starts.
    foreach($gpu in $GpuTargets) {
        & cmake --build $build --config $config --target $gpu --parallel 1 *> (Join-Path $out ($gpu+'.log'))
        if($LASTEXITCODE -ne 0){throw "GPU target failed: $gpu; evidence $out"}
    }
    # CPU tests share temporary fixtures, so explicitly override CTEST_PARALLEL_LEVEL.
    $run=@('--test-dir',$build,'-C',$config,'--output-on-failure','--parallel','1')
    if($Level -eq 'Focused'){$run+=@('-L',('^'+$Area+'$'))}
    & ctest @run *> (Join-Path $out 'ctest.log')
    if($LASTEXITCODE -ne 0){throw "CTest failed: $out"}
    if($Level -eq 'Release') {
        # Product and selected tests were built above; CPack need not rebuild ALL.
        & cpack -C $config --config (Join-Path $build 'CPackConfig.cmake') -B (Join-Path $root '.local/packages') *> (Join-Path $out 'package.log')
        if($LASTEXITCODE -ne 0){throw "Package failed: $out"}
    }
    Write-Output "Iris $Compiler / $Level / $Area PASS: $(@($tests).Count) CPU tests; evidence $out"
} finally {Pop-Location}
