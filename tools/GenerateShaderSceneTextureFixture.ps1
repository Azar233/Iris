param([string]$Directory='assets/shader-scenes/textures')
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force $Directory | Out-Null
Add-Type -AssemblyName System.Drawing
$image=New-Object Drawing.Bitmap 8,8
try {
    for($y=0;$y -lt 8;$y++){for($x=0;$x -lt 8;$x++){
        $color=if((($x+$y)%2) -eq 0){[Drawing.Color]::FromArgb(255,128,64,192)}else{[Drawing.Color]::FromArgb(255,32,192,128)}
        $image.SetPixel($x,$y,$color)
    }}
    $image.Save((Join-Path (Resolve-Path $Directory).Path 'palette.png'),[Drawing.Imaging.ImageFormat]::Png)
} finally {$image.Dispose()}
[byte[]]$volume=New-Object byte[] 64
for($z=0;$z -lt 4;$z++){for($y=0;$y -lt 4;$y++){for($x=0;$x -lt 4;$x++){
    $volume[$x+4*($y+4*$z)]=[byte](16+12*$x+20*$y+40*$z)
}}}
[IO.File]::WriteAllBytes((Join-Path (Resolve-Path $Directory).Path 'volume.raw'),$volume)
