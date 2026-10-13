param([ValidateSet('before','after')][string]$Phase='after', [string]$BuildDirectory='build-ci-msvc',
    [string]$OutputDirectory='.local/verification/a4n1', [string[]]$Cases=@())
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
    $env:MYRENDERER_STYLIZED_OUTLINE='1';$env:MYRENDERER_STYLIZED_BANDS='3'
    foreach($path in @(0,1)) {
        foreach($mode in @('pbr','toon','bands','rim')) {
            $name="$mode-$path"
            if($Cases.Count -and $name -notin $Cases){continue}
            $env:MYRENDERER_RENDER_PATH=[string]$path
            $env:MYRENDERER_STYLIZED=if($mode -eq 'pbr'){'0'}else{'1'}
            $env:MYRENDERER_STYLIZED_DEBUG=if($mode -eq 'bands'){'1'}elseif($mode -eq 'rim'){'2'}else{'0'}
            $env:MYRENDERER_SCREENSHOT=Join-Path $out ($name+'.png')
            if($mode -eq 'toon') {
                Remove-Item Env:MYRENDERER_SMOKE_TEST -ErrorAction SilentlyContinue
                $env:MYRENDERER_BENCHMARK_WARMUP='120';$env:MYRENDERER_BENCHMARK_FRAMES='900'
                $env:MYRENDERER_BENCHMARK_OUTPUT=Join-Path $out ($name+'.json')
            }else {
                $env:MYRENDERER_SMOKE_TEST='1'
                Remove-Item Env:MYRENDERER_BENCHMARK_OUTPUT -ErrorAction SilentlyContinue
                Remove-Item Env:MYRENDERER_BENCHMARK_FRAMES -ErrorAction SilentlyContinue
                Remove-Item Env:MYRENDERER_BENCHMARK_WARMUP -ErrorAction SilentlyContinue
            }
            & $exe (Join-Path $root 'assets/scenes/fixtures/14_polyhaven_material_gallery.myscene') *> (Join-Path $out ($name+'.log'))
            if($LASTEXITCODE -ne 0 -or -not(Test-Path $env:MYRENDERER_SCREENSHOT)){throw "NPR capture failed: $name"}
            if($Phase -eq 'after') {
                $before=Join-Path (Join-Path (Join-Path $root $OutputDirectory) ('before-'+$BuildDirectory)) ($name+'.png')
                if((Get-FileHash $before).Hash -ne (Get-FileHash $env:MYRENDERER_SCREENSHOT).Hash){throw "NPR migration changed pixels: $name"}
                if($mode -eq 'toon') {
                    $old=Get-Content ([IO.Path]::ChangeExtension($before,'.json')) -Raw | ConvertFrom-Json
                    $new=Get-Content (Join-Path $out ($name+'.json')) -Raw | ConvertFrom-Json
                    if($new.drawCalls -ne $old.drawCalls -or $new.renderMemoryBytes -ne $old.renderMemoryBytes -or
                        $new.cpuFrameP95Ms -gt [Math]::Max($old.cpuFrameP95Ms*1.25,$old.cpuFrameP95Ms+.5) -or
                        $new.gpuFrameP95Ms -gt [Math]::Max($old.gpuFrameP95Ms*1.25,$old.gpuFrameP95Ms+.1)) {
                        throw "NPR migration exceeded draw/memory or fixed cost budget: $name"
                    }
                }
            }
        }
    }
    Write-Output "NPR $Phase fixed Forward/Deferred PBR/toon/bands/rim and cost capture PASS: $out"
}finally {
    Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
