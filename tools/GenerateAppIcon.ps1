param(
    [Parameter(Mandatory = $true)]
    [string]$Source,
    [string]$PngOutput = "assets/icons/iris-icon.png",
    [string]$IcoOutput = "assets/icons/iris.ico"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Remove-DetachedPixels {
    param([System.Drawing.Bitmap]$Bitmap)

    $width = $Bitmap.Width
    $height = $Bitmap.Height
    $visited = New-Object bool[] ($width * $height)
    $largest = New-Object 'System.Collections.Generic.List[int]'
    $queue = New-Object 'System.Collections.Generic.Queue[int]'

    for ($start = 0; $start -lt $visited.Length; ++$start) {
        if ($visited[$start]) { continue }
        $x = $start % $width
        $y = [Math]::Floor($start / $width)
        if ($Bitmap.GetPixel($x, $y).A -eq 0) {
            $visited[$start] = $true
            continue
        }

        $component = New-Object 'System.Collections.Generic.List[int]'
        $visited[$start] = $true
        $queue.Enqueue($start)
        while ($queue.Count -gt 0) {
            $index = $queue.Dequeue()
            $component.Add($index)
            $cx = $index % $width
            $cy = [Math]::Floor($index / $width)
            foreach ($neighbor in @(
                $(if ($cx -gt 0) { $index - 1 }),
                $(if ($cx + 1 -lt $width) { $index + 1 }),
                $(if ($cy -gt 0) { $index - $width }),
                $(if ($cy + 1 -lt $height) { $index + $width })
            )) {
                if ($null -eq $neighbor -or $visited[$neighbor]) { continue }
                $visited[$neighbor] = $true
                $nx = $neighbor % $width
                $ny = [Math]::Floor($neighbor / $width)
                if ($Bitmap.GetPixel($nx, $ny).A -gt 0) {
                    $queue.Enqueue($neighbor)
                }
            }
        }
        if ($component.Count -gt $largest.Count) {
            $largest = $component
        }
    }

    $keep = New-Object bool[] ($width * $height)
    foreach ($index in $largest) { $keep[$index] = $true }
    for ($index = 0; $index -lt $keep.Length; ++$index) {
        if (-not $keep[$index]) {
            $Bitmap.SetPixel($index % $width, [Math]::Floor($index / $width), [System.Drawing.Color]::Transparent)
        }
    }
}

function New-ResizedPngBytes {
    param(
        [System.Drawing.Image]$Image,
        [int]$Size
    )

    $bitmap = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bitmap.SetResolution(96, 96)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.DrawImage($Image, 0, 0, $Size, $Size)
    }
    finally {
        $graphics.Dispose()
    }

    try {
        Remove-DetachedPixels -Bitmap $bitmap
        $stream = New-Object System.IO.MemoryStream
        try {
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            return ,$stream.ToArray()
        }
        finally {
            $stream.Dispose()
        }
    }
    finally {
        $bitmap.Dispose()
    }
}

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$pngPath = [System.IO.Path]::GetFullPath((Join-Path $PWD $PngOutput))
$icoPath = [System.IO.Path]::GetFullPath((Join-Path $PWD $IcoOutput))
$sourceImage = [System.Drawing.Image]::FromFile($sourcePath)
try {
    [System.IO.File]::WriteAllBytes($pngPath, (New-ResizedPngBytes -Image $sourceImage -Size 256))

    $sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
    $images = @($sizes | ForEach-Object { New-ResizedPngBytes -Image $sourceImage -Size $_ })
    $stream = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter($stream)
    try {
        $writer.Write([UInt16]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($index = 0; $index -lt $sizes.Count; ++$index) {
            $sizeByte = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
            $writer.Write([Byte]$sizeByte)
            $writer.Write([Byte]$sizeByte)
            $writer.Write([Byte]0)
            $writer.Write([Byte]0)
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]32)
            $writer.Write([UInt32]$images[$index].Length)
            $writer.Write([UInt32]$offset)
            $offset += $images[$index].Length
        }
        foreach ($image in $images) {
            $writer.Write($image)
        }
        [System.IO.File]::WriteAllBytes($icoPath, $stream.ToArray())
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}
finally {
    $sourceImage.Dispose()
}

Write-Output "Generated $PngOutput and $IcoOutput"
