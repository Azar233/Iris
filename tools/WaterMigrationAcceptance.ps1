param([ValidateSet('before','after')][string]$Phase='after', [string]$BuildDirectory='build-ci-msvc',
    [string]$OutputDirectory='.local/verification/a4w1', [string[]]$Cases=@())
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root $BuildDirectory
$exe=if(Test-Path (Join-Path $build 'Release/Iris.exe')){Join-Path $build 'Release/Iris.exe'}else{Join-Path $build 'Iris.exe'}
$out=Join-Path (Join-Path $root $OutputDirectory) ($Phase+'-'+$BuildDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {
    $saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)
}
try {
    $env:IRIS_STATE_DIRECTORY=Join-Path $out 'state'
    $env:MYRENDERER_RENDER_WIDTH='640';$env:MYRENDERER_RENDER_HEIGHT='360';$env:MYRENDERER_MSAA='1'
    $env:MYRENDERER_ANIMATION_TIME='0';$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
    $waterCases=@(
        @{name='low-0';scene='21_ocean_depth';path=0;quality=0;optics=0;preset=3},
        @{name='low-1';scene='21_ocean_depth';path=1;quality=0;optics=0;preset=3},
        @{name='high-0';scene='21_ocean_depth';path=0;quality=1;optics=0;preset=3},
        @{name='high-1';scene='21_ocean_depth';path=1;quality=1;optics=0;preset=3},
        @{name='optics-0';scene='21_ocean_depth';path=0;quality=1;optics=1;preset=3},
        @{name='optics-1';scene='21_ocean_depth';path=1;quality=1;optics=1;preset=3},
        @{name='underwater-0';scene='22_ocean_underwater';path=0;quality=1;optics=1;preset=1},
        @{name='underwater-1';scene='22_ocean_underwater';path=1;quality=1;optics=1;preset=1}
    )
    $captured=0
    foreach($case in $waterCases) {
        $name=$case.name
        if($Cases.Count -and $name -notin $Cases){continue}
        $env:MYRENDERER_RENDER_PATH=[string]$case.path;$env:MYRENDERER_WATER='1'
        $env:MYRENDERER_WATER_QUALITY=[string]$case.quality;$env:MYRENDERER_WATER_PRESET=[string]$case.preset
        $env:MYRENDERER_WATER_SURFACE_OPTICS=[string]$case.optics
        $env:MYRENDERER_SCREENSHOT=Join-Path $out ($name+'.png')
        $cost=$name -like 'high-*'
        if($cost) {
            Remove-Item Env:MYRENDERER_SMOKE_TEST -ErrorAction SilentlyContinue
            $env:MYRENDERER_BENCHMARK_WARMUP='120';$env:MYRENDERER_BENCHMARK_FRAMES='900'
            $env:MYRENDERER_BENCHMARK_OUTPUT=Join-Path $out ($name+'.json')
        }else {
            $env:MYRENDERER_SMOKE_TEST='1'
            Remove-Item Env:MYRENDERER_BENCHMARK_OUTPUT -ErrorAction SilentlyContinue
            Remove-Item Env:MYRENDERER_BENCHMARK_FRAMES -ErrorAction SilentlyContinue
            Remove-Item Env:MYRENDERER_BENCHMARK_WARMUP -ErrorAction SilentlyContinue
        }
        & $exe (Join-Path $root ('assets/scenes/fixtures/'+$case.scene+'.myscene')) *> (Join-Path $out ($name+'.log'))
        if($LASTEXITCODE -ne 0 -or -not(Test-Path $env:MYRENDERER_SCREENSHOT)){throw "Water capture failed: $name"}
        ++$captured
        if($Phase -eq 'after') {
            $before=Join-Path (Join-Path (Join-Path $root $OutputDirectory) ('before-'+$BuildDirectory)) ($name+'.png')
            if((Get-FileHash $before).Hash -ne (Get-FileHash $env:MYRENDERER_SCREENSHOT).Hash){throw "Water migration changed pixels: $name"}
            if($cost) {
                $old=Get-Content ([IO.Path]::ChangeExtension($before,'.json')) -Raw | ConvertFrom-Json
                $new=Get-Content (Join-Path $out ($name+'.json')) -Raw | ConvertFrom-Json
                if($new.drawCalls -ne $old.drawCalls -or $new.renderMemoryBytes -ne $old.renderMemoryBytes -or
                    $new.cpuFrameP95Ms -gt [Math]::Max($old.cpuFrameP95Ms*1.25,$old.cpuFrameP95Ms+.5) -or
                    $new.gpuFrameP95Ms -gt [Math]::Max($old.gpuFrameP95Ms*1.25,$old.gpuFrameP95Ms+.1)) {
                    throw "Water migration exceeded fixed resource/cost budget: $name"
                }
            }
        }
    }
    if($captured -eq 0){throw 'No water migration cases executed'}
    Write-Output "Water $Phase fixed Forward/Deferred Low/High/optics/underwater and cost capture PASS: $out"
}finally {
    Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
