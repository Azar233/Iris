param([string]$BuildDirectory='build-ci-msvc')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root "$BuildDirectory/m2c-water"
$media=Join-Path $root 'docs/media'
$pairs=@(@('legacy-optics','deferred-msaa4-taa1','同参数：旧水面光学','新水面：分离太阳、过滤反射'),
    @('near-below-legacy','near-below-filtered','水下 2 cm：旧雾误算 24 m','水下 2 cm：雾止于水面'))
$canvas=[System.Drawing.Bitmap]::new(1280,784)
$graphics=[System.Drawing.Graphics]::FromImage($canvas)
$font=[System.Drawing.Font]::new('Microsoft YaHei',15)
try {
    $graphics.Clear([System.Drawing.Color]::FromArgb(24,28,35))
    $graphics.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    for($row=0;$row -lt 2;$row++) {
        for($column=0;$column -lt 2;$column++) {
            $bitmap=[System.Drawing.Bitmap]::new((Join-Path $output ($pairs[$row][$column]+'.png')))
            try {$graphics.DrawImage($bitmap,[System.Drawing.Rectangle]::new($column*640,$row*392+32,640,360))} finally {$bitmap.Dispose()}
            $graphics.DrawString($pairs[$row][$column+2],$font,[System.Drawing.Brushes]::White,$column*640+8,$row*392+4)
        }
    }
    $canvas.Save((Join-Path $media 'm2c-water-before-after.png'),[System.Drawing.Imaging.ImageFormat]::Png)
} finally {$font.Dispose();$graphics.Dispose();$canvas.Dispose()}
foreach($name in 'noon-high','sunset-high','night-high','depth-filtered') {
    Copy-Item -LiteralPath (Join-Path $output ($name+'.png')) -Destination (Join-Path $media ('m2c-'+$name+'.png'))
}
