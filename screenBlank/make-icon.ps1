# Regenerates screenBlank.ico (monitor with a dark screen and a power symbol).
# Only needed if you change the design; the .ico is committed.
Add-Type -AssemblyName System.Drawing

$sizes = 16, 24, 32, 48, 64, 128, 256
$pngs = foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)
    $u = $s / 16.0

    $frame = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 70, 76, 86))
    $screen = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 6, 7, 9))
    $accent = [System.Drawing.Color]::FromArgb(255, 47, 128, 237)

    function RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
        $p = New-Object System.Drawing.Drawing2D.GraphicsPath
        $d = [Math]::Max(0.01, $r * 2)
        $p.AddArc($x, $y, $d, $d, 180, 90)
        $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
        $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
        $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
        $p.CloseFigure()
        $p
    }

    $g.FillPath($frame, (RoundRect (0.4 * $u) (1.6 * $u) (15.2 * $u) (10.6 * $u) (1.2 * $u)))
    $g.FillPath($screen, (RoundRect (1.5 * $u) (2.7 * $u) (13.0 * $u) (8.4 * $u) (0.5 * $u)))
    $g.FillRectangle($frame, 6.8 * $u, 12.0 * $u, 2.4 * $u, 1.6 * $u)
    $g.FillPath($frame, (RoundRect (4.2 * $u) (13.4 * $u) (7.6 * $u) (1.6 * $u) (0.6 * $u)))

    # Power symbol in the screen
    $pw = [Math]::Max(1.0, 0.95 * $u)
    $pen = New-Object System.Drawing.Pen $accent, $pw
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    $cx = 8.0 * $u; $cy = 7.1 * $u; $r = 2.5 * $u
    $g.DrawArc($pen, $cx - $r, $cy - $r, 2 * $r, 2 * $r, -60, 300)
    $g.DrawLine($pen, $cx, $cy - $r - 0.4 * $u, $cx, $cy - 0.4 * $u)

    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    , $ms.ToArray()
}

$out = Join-Path $PSScriptRoot 'screenBlank.ico'
$fs = [System.IO.File]::Create($out)
$w = New-Object System.IO.BinaryWriter $fs
$w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $dim = if ($s -ge 256) { 0 } else { $s }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([UInt16]1); $w.Write([UInt16]32)
    $w.Write([UInt32]$pngs[$i].Length); $w.Write([UInt32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
$w.Close()
Write-Host "Wrote $out"
