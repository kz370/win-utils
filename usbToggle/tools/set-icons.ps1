<#
.SYNOPSIS
    Makes the three icon files in assets\ from a PNG or SVG image.

.DESCRIPTION
    Writes:
      assets\app_icon.ico     window, taskbar, Start menu, .exe and installer
      assets\tray_icon.ico    tray icon (notification area), frames 16-64 px
      assets\header_logo.png  logo at the top of the app window

    You can also just replace those files by hand with your own (an .ico must
    contain several sizes; the tray picks the one that fits the screen scale).

    Nothing is changed unless you give an image or -Reset.

.PARAMETER AppImage
    Square PNG or SVG (512x512 or larger, transparent background). Used for
    all three files unless -TrayImage is also given.

.PARAMETER TrayImage
    Optional separate PNG or SVG for the tray icon.

.PARAMETER Reset
    Rebuild the three files from the built-in artwork in assets\source\.

.EXAMPLE
    .\set-icons.ps1 C:\pictures\my-icon.png
.EXAMPLE
    .\set-icons.ps1 C:\pictures\app.png C:\pictures\tray.png
.EXAMPLE
    .\set-icons.ps1 -Reset
#>
param(
    [string]$AppImage,
    [string]$TrayImage,
    [switch]$Reset
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$Root   = Split-Path -Parent $PSScriptRoot
$Assets = Join-Path $Root 'assets'
$Source = Join-Path $Assets 'source'

$AppFrames   = 32, 40, 48, 64, 128, 256
$SmallFrames = 16, 20, 24
$TraySizes   = 16, 20, 24, 32, 40, 48, 64

function Say([string]$text) { Write-Host "[icons] $text" }

if (-not $AppImage -and -not $Reset) {
    Write-Host 'Usage:'
    Write-Host '  set-icons.bat my-icon.png          one image for everything (or drag it onto set-icons.bat)'
    Write-Host '  set-icons.bat app.png tray.png     a separate image for the tray'
    Write-Host '  set-icons.bat --reset              back to the built-in icons'
    Write-Host ''
    Write-Host 'This replaces assets\app_icon.ico, assets\tray_icon.ico and assets\header_logo.png.'
    Write-Host 'Images: square PNG or SVG, 512x512 or larger, transparent background.'
    exit 0
}

function Test-Image([string]$path) {
    if (-not (Test-Path $path)) { throw "Image not found: $path" }
    $full = (Resolve-Path $path).Path
    $ext = [IO.Path]::GetExtension($full).ToLowerInvariant()
    if ($ext -ne '.png' -and $ext -ne '.svg') {
        throw "Use a square PNG or SVG image (got '$ext'). To use your own .ico, copy it into assets\ as app_icon.ico or tray_icon.ico."
    }
    if ($ext -eq '.png') {
        $img = [Drawing.Image]::FromFile($full)
        try {
            if ($img.Width -ne $img.Height) { throw "The image must be square (it is $($img.Width)x$($img.Height))." }
            if ($img.Width -lt 256) { Write-Warning "The image is only $($img.Width)x$($img.Height) px; 512x512 or larger looks best." }
        } finally { $img.Dispose() }
    }
    return $full
}

# Rasterizes a PNG/SVG into <size>x<size>.png files using the Tauri CLI.
function Invoke-TauriIcon([string]$src, [string]$out, [int[]]$sizes) {
    $cmdArgs = @('/c', 'npx', 'tauri', 'icon', "`"$src`"", '-o', "`"$out`"")
    foreach ($s in $sizes) { $cmdArgs += @('-p', "$s") }
    $p = Start-Process -FilePath 'cmd.exe' -ArgumentList $cmdArgs -WorkingDirectory $Root -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "tauri icon failed for $src (exit code $($p.ExitCode))." }
}

# Writes a multi-size .ico whose frames are PNG images (supported since Windows Vista).
function Write-Ico([string]$path, [string[]]$pngFiles) {
    $frames = @()
    foreach ($f in $pngFiles) {
        $img = [Drawing.Image]::FromFile($f)
        $size = $img.Width
        $img.Dispose()
        $frames += [pscustomobject]@{ Size = $size; Bytes = [IO.File]::ReadAllBytes($f) }
    }
    $frames = @($frames | Sort-Object Size)
    $stream = [IO.File]::Create($path)
    $w = New-Object IO.BinaryWriter($stream)
    try {
        $w.Write([uint16]0)                   # reserved
        $w.Write([uint16]1)                   # type: icon
        $w.Write([uint16]$frames.Count)
        $offset = 6 + 16 * $frames.Count
        foreach ($fr in $frames) {
            $dim = 0
            if ($fr.Size -lt 256) { $dim = $fr.Size }
            $w.Write([byte]$dim)              # width (0 means 256)
            $w.Write([byte]$dim)              # height
            $w.Write([byte]0)                 # palette colors
            $w.Write([byte]0)                 # reserved
            $w.Write([uint16]1)               # color planes
            $w.Write([uint16]32)              # bits per pixel
            $w.Write([uint32]$fr.Bytes.Length)
            $w.Write([uint32]$offset)
            $offset += $fr.Bytes.Length
        }
        foreach ($fr in $frames) { $w.Write([byte[]]$fr.Bytes) }
    } finally {
        $w.Close()
    }
}

# ------------------------------------------------------------------ choose images

if ($Reset) {
    $app   = Join-Path $Source 'app_icon.svg'
    $small = Join-Path $Source 'app_icon_small.svg'
    $tray  = Join-Path $Source 'tray_icon.svg'
    foreach ($f in $app, $small, $tray) { if (-not (Test-Path $f)) { throw "Missing built-in artwork: $f" } }
    Say 'Using the built-in icons.'
} else {
    $app = Test-Image $AppImage
    $small = $app                                   # one image: small frames come from it too
    $tray = $app
    if ($TrayImage) { $tray = Test-Image $TrayImage }
    Say "App image:  $app"
    Say "Tray image: $tray"
}

if (-not (Test-Path (Join-Path $Root 'node_modules'))) {
    Say 'Installing the Tauri CLI (first run only)...'
    $p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', 'npm', 'install' -WorkingDirectory $Root -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw 'npm install failed. Is Node.js installed?' }
}

# ------------------------------------------------------------------ generate
# Everything is made in a temporary folder first, so a failure leaves the
# existing icons untouched.

$tmp = Join-Path ([IO.Path]::GetTempPath()) ('usbToggle-icons-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    Say 'Rendering the app icon...'
    Invoke-TauriIcon $app (Join-Path $tmp 'app') $AppFrames
    Invoke-TauriIcon $small (Join-Path $tmp 'small') $SmallFrames
    Say 'Rendering the tray icon...'
    Invoke-TauriIcon $tray (Join-Path $tmp 'tray') $TraySizes

    $appFiles = @()
    foreach ($s in $SmallFrames) { $appFiles += Join-Path $tmp "small\${s}x${s}.png" }
    foreach ($s in $AppFrames)   { $appFiles += Join-Path $tmp "app\${s}x${s}.png" }
    $trayFiles = @()
    foreach ($s in $TraySizes)   { $trayFiles += Join-Path $tmp "tray\${s}x${s}.png" }

    New-Item -ItemType Directory -Path $Assets -Force | Out-Null
    Write-Ico (Join-Path $tmp 'app_icon.ico') $appFiles
    Write-Ico (Join-Path $tmp 'tray_icon.ico') $trayFiles

    Copy-Item (Join-Path $tmp 'app_icon.ico') (Join-Path $Assets 'app_icon.ico') -Force
    Copy-Item (Join-Path $tmp 'tray_icon.ico') (Join-Path $Assets 'tray_icon.ico') -Force
    Copy-Item (Join-Path $tmp 'app\256x256.png') (Join-Path $Assets 'header_logo.png') -Force
} finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

Say 'Done: assets\app_icon.ico, assets\tray_icon.ico and assets\header_logo.png were replaced.'
Say 'Run build.bat to build the app with the new icons.'
Say 'If Windows still shows the old icon on the taskbar or desktop, unpin and re-pin the app (Windows caches icons).'
