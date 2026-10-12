param([string]$BuildDirectory='build-ci-msvc',[string]$RuntimeDirectory,[switch]$SingleWindow)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$runtime=if($RuntimeDirectory){[IO.Path]::GetFullPath($RuntimeDirectory)}else{Join-Path $root $BuildDirectory}
$exe=if(Test-Path (Join-Path $runtime 'Release/Iris.exe')){Join-Path $runtime 'Release/Iris.exe'}else{Join-Path $runtime 'Iris.exe'}
$inputs=if($RuntimeDirectory){$runtime}else{$root}
$out=Join-Path $root ('.local/verification/s1c2/'+$BuildDirectory+'/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {
    $saved[$_.Name]=$_.Value;Remove-Item -LiteralPath ('Env:'+$_.Name)
}
function Invoke-Iris([string[]]$Arguments,[string]$Log){
    $info=New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName=$exe;$info.Arguments=($Arguments | ForEach-Object {'"'+$_+'"'}) -join ' '
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WorkingDirectory=$inputs
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $process=New-Object System.Diagnostics.Process;$process.StartInfo=$info
    if(-not $process.Start()){throw 'Cannot start Iris'}
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    try {
        if(-not $process.WaitForExit(180000)){throw 'Iris delivery acceptance timed out'}
        $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        $text | Set-Content -Encoding UTF8 $Log
        if($process.ExitCode -ne 0){throw "Iris failed ($($process.ExitCode)): $Log"}
        return $text
    }finally{$process.Dispose()}
}
try {
    $env:IRIS_STATE_DIRECTORY=Join-Path $out 'state'
    New-Item -ItemType Directory -Force $env:IRIS_STATE_DIRECTORY | Out-Null
    Set-Content (Join-Path $env:IRIS_STATE_DIRECTORY 'language') en
    $widths=if($SingleWindow){@(1440)}else{@(1440,1100)}
    foreach($package in 'sequence','enscape'){
        $packagePath=Join-Path $inputs "assets/shader-scenes/$package/$package.shaderscene"
        foreach($width in $widths){
            $case=Join-Path $out "$package-$width"
            $env:MYRENDERER_SMOKE_TEST='1';$env:MYRENDERER_RENDER_WIDTH='320';$env:MYRENDERER_RENDER_HEIGHT='180'
            $env:MYRENDERER_EDITOR_WINDOW_WIDTH=[string]$width
            $env:MYRENDERER_EDITOR_WINDOW_HEIGHT=if($width -eq 1440){'900'}else{'680'}
            $env:MYRENDERER_EDITOR_SCREENSHOT_TAB='render-queue'
            $env:MYRENDERER_EDITOR_SCREENSHOT=Join-Path $out "$package-$width.png"
            $env:IRIS_SHADER_SEQUENCE_ACCEPTANCE_DIRECTORY=$case
            $env:IRIS_SHADER_SEQUENCE_ACCEPTANCE_PACKAGE=$packagePath
            if($package -eq 'sequence'){$env:IRIS_SHADER_SEQUENCE_FAILURE_ACCEPTANCE='1'}
            $text=Invoke-Iris @($packagePath) (Join-Path $out "$package-$width.log")
            if($text -notmatch 'six-frame PNG parity / safe cancellation: PASS' -or
                ($package -eq 'sequence' -and $text -notmatch 'bounded Shader failure diagnostics / repaired package resubmission: PASS') -or
                -not(Test-Path $env:MYRENDERER_EDITOR_SCREENSHOT)){throw 'GUI/Queue acceptance evidence missing'}
            if($package -eq 'sequence'){
                $diagnostic=Get-Content (Join-Path $case 'queue-error.txt') -Raw
                if($diagnostic -notmatch 'Pass Accum' -or $diagnostic -notmatch 'accum.frag:3' -or
                    $diagnostic -notmatch '\[middle executor output truncated\]' -or $diagnostic.Length -ge 17000){
                    throw 'Queue diagnostic lost original Pass/file/line or truncation boundary'}
            }
            Get-ChildItem Env: | Where-Object {($_.Name -like 'IRIS_*' -and $_.Name -ne 'IRIS_STATE_DIRECTORY') -or $_.Name -like 'MYRENDERER_*'} |
                ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
            foreach($run in 'cli-one','cli-two'){
                $null=Invoke-Iris @('raster-sequence',(Join-Path $case 'sequence.renderjob'),'--output',(Join-Path $case "$run/frame_{frame:04}")) (Join-Path $out "$package-$width-$run.log")
                foreach($frame in 3..8){
                    $stem='frame_{0:0000}' -f $frame
                    foreach($suffix in '.png','-report.json'){
                        if((Get-FileHash (Join-Path $case "queued/$stem$suffix")).Hash -ne
                            (Get-FileHash (Join-Path $case "$run/$stem$suffix")).Hash){throw "GUI/Queue/CLI mismatch: $package $frame$suffix"}
                    }
                }
            }
            $report=Get-Content (Join-Path $case 'queued/frame_0003-report.json') -Raw | ConvertFrom-Json
            if(-not $report.inputManifest.complete -or -not $report.shaderScene.buildId -or -not $report.gpu -or
                $report.shaderScene.textures.encoding -notcontains 'hash-v1' -or -not $report.shaderScene.passes.channels -or
                $report.timeSeconds -ne 0.125 -or $report.shaderScene.sampleIndex -ne 3){throw 'Incomplete package report identity'}
            if($package -eq 'enscape' -and ($report.shaderScene.parameterSource -ne 'iris.enscape-study' -or
                $report.shaderScene.textures.encoding -notcontains 'raw' -or
                -not($report.inputManifest.files.path -match 'weather.raw') -or
                -not($report.inputManifest.files.path -match 'buffer_c.glsl'))){throw 'Enscape input identity missing'}
            if($RuntimeDirectory){
                $example=if($package -eq 'sequence'){'08_shader_scene_sequence'}else{'09_enscape_shader_sequence'}
                $null=Invoke-Iris @('raster-sequence',(Join-Path $runtime "assets/renderjobs/$example.renderjob"),
                    '--output',(Join-Path $case 'installed-example/frame_{frame:04}')) (Join-Path $out "$package-installed-example.log")
                foreach($frame in 3..8){$stem='frame_{0:0000}.png' -f $frame
                    if((Get-FileHash (Join-Path $case "queued/$stem")).Hash -ne
                        (Get-FileHash (Join-Path $case "installed-example/$stem")).Hash){throw 'Shipped example Job differs from GUI sequence'}}
                $exampleReport=Get-Content (Join-Path $case 'installed-example/frame_0003-report.json') -Raw | ConvertFrom-Json
                if(-not $exampleReport.inputManifest.complete){throw 'Installed example binary/input identity incomplete'}
                foreach($file in $exampleReport.inputManifest.files){
                    if(-not([IO.Path]::GetFullPath($file.path).StartsWith($runtime+'\',[StringComparison]::OrdinalIgnoreCase))){
                        throw "Shipped example consumed source-tree input: $($file.path)"}
                }
                foreach($file in $report.inputManifest.files){
                    $path=[IO.Path]::GetFullPath($file.path)
                    if(-not($path.StartsWith($runtime+'\',[StringComparison]::OrdinalIgnoreCase) -or
                        $path.StartsWith($out+'\',[StringComparison]::OrdinalIgnoreCase))){throw "Installed run consumed source-tree input: $path"}
                }
                $license=if($package -eq 'sequence'){'LICENSE'}else{'LICENSE.md'}
                foreach($file in Get-ChildItem (Join-Path $root "assets/shader-scenes/$package") -File){
                    if((Get-FileHash $file.FullName).Hash -ne (Get-FileHash (Join-Path (Split-Path $packagePath) $file.Name)).Hash){throw "Installed package differs: $($file.Name)"}
                }
                if(-not(Test-Path (Join-Path (Split-Path $packagePath) $license)) -or -not(Test-Path (Join-Path $runtime 'ASSET_LICENSES.md'))){throw 'Installed licenses missing'}
            }
        }
    }
    Write-Output "ShaderScene dual-package GUI/Queue/CLI, reports, failure/recovery and cancellation PASS: $out"
}finally{
    Get-ChildItem Env: | Where-Object {$_.Name -like 'IRIS_*' -or $_.Name -like 'MYRENDERER_*'} | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
    foreach($key in $saved.Keys){Set-Item -LiteralPath ('Env:'+$key) -Value $saved[$key]}
}
