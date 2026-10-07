param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
function Read-WaterMetrics([string]$Name) {
    $bitmap=[System.Drawing.Bitmap]::new((Join-Path $OutputDirectory ($Name+'.png')))
    try {
        $white=0; $count=0; $sum=0.0
        # Freeze the lower third: no sky, solar disk, or horizon silhouette.
        for($y=[int]($bitmap.Height*2/3);$y -lt $bitmap.Height;$y++) {
            for($x=0;$x -lt $bitmap.Width;$x++) {
                $c=$bitmap.GetPixel($x,$y)
                if($c.R -ge 235 -and $c.G -ge 235 -and $c.B -ge 235){$white++}
                $sum+=(0.2126*$c.R+0.7152*$c.G+0.0722*$c.B)/255.0; $count++
            }
        }
        return [pscustomobject]@{Name=$Name;Pixels=$count;WhiteFraction=$white/[double]$count;MeanLuminance=$sum/$count}
    } finally {$bitmap.Dispose()}
}
$before=Read-WaterMetrics 'legacy-optics'
$after=Read-WaterMetrics 'deferred-msaa4-taa1'
if($before.WhiteFraction -le 0 -or $after.WhiteFraction -gt $before.WhiteFraction*0.5) {
    throw 'Filtered optics must cut saturated white water pixels by at least 50% at unchanged camera/exposure/material'
}
if($after.MeanLuminance -lt 0.05) {throw 'Removing solar outliers must not produce a black sea'}
[pscustomobject]@{Region='lower third';WhiteThreshold=235;Before=$before;After=$after;Reduction=1-$after.WhiteFraction/$before.WhiteFraction} |
    ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutputDirectory 'image-metrics.json')
Write-Output 'Water image semantic checks: PASS'
