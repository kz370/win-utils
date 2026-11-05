<#
.SYNOPSIS
    Regenerates every USB Toggle icon from one or two images.

.DESCRIPTION
    Produces, from square PNG or SVG images:
      - src-tauri\icons\icon.ico          window, taskbar, Start menu, installer, exe
      - src-tauri\icons\*.png, icon.icns   the rest of the Tauri icon set
      - src-tauri\icons\tray\<size>.png    tray icon, one file per DPI size
      - ui\logo.png                        logo in the window header

    Images are looked up in src-tauri\icons\source\ (your images) and then in
    src-tauri\icons\source\default\ (the built-in artwork):

      app    source\icon.png|svg        else default\icon.svg
      tray   source\tray.png|svg        else your app image, or default\tray.svg
      small  source\icon-small.png|svg  else your app image, or default\icon-small.svg
             (small = the 16/20/24 px frames of icon.ico)

    Images given on the command line are copied into source\ first, so later
    runs and other people's builds use them too.

.PARAMETER AppImage
    New app image. Given alone, it is used for every icon, including the tray.

.PARAMETER TrayImage
    New tray image (optional).

.PARAMETER Reset
    Remove your images from source\ and go back to the built-in artwork.

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

$Root     = Split-Path -Parent $PSScriptRoot
$Icons    = Join-Path $Root 'src-tauri\icons'
$Source   = Join-Path $Icons 'source'
$Defaults = Join-Path $Source 'default'
$TrayDir  = Join-Path $Icons 'tray'
$Ui       = Join-Path $Root 'ui'

$AppFrames   = 32, 40, 48, 64, 128, 256
$SmallFrames = 16, 20, 24
$TraySizes   = 16, 20, 24, 32, 40, 48, 64

function Say([string]$text) { Write-Host "[icons] $text" }

# Your image for a name (PNG preferred over SVG), or $null.
function Find-Custom([string]$name) {
    foreach ($ext in 'png', 'svg') {
        $path = Join-Path $Source "$name.$ext"
        if (Test-Path $path) { return $path }
    }
    return $null
}

function Remove-Custom([string]$name) {
    foreach ($ext in 'png', 'svg') {
        Remove-Item (Join-Path $Source "$name.$ext") -ErrorAction SilentlyContinue
    }
}

function Import-Image([string]$path, [string]$name) {
    if (-not (Test-Path $path)) { throw "Image not found: $path" }
    $full = (Resolve-Path $path).Path
    $ext = [IO.Path]::GetExtension($full).ToLowerInvariant()
    if ($ext -ne '.png' -and $ext -ne '.svg') {
        throw "Use a square PNG or SVG image (got '$ext'). Convert JPG/ICO files to PNG first."
    }
    if ($ext -eq '.png') {
        $img = [Drawing.Image]::FromFile($full)
        try {
            if ($img.Width -ne $img.Height) {
                throw "The image must be square (it is $($img.Width)x$($img.Height))."
            }
            if ($img.Width -lt 256) {
                Write-Warning "The image is only $($img.Width)x$($img.Height) px; 512x512 or larger looks best."
            }
        } finally { $img.Dispose() }
    }
    $dest = Join-Path $Source "$name$ext"
    if ($full -ieq $dest) { return }   # already in place
    Remove-Custom $name
    Copy-Item $full $dest -Force
    Say "Using $full for the $name icon"
}

# Runs the Tauri CLI's icon generator, which rasterizes PNG and SVG sources.
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

# ------------------------------------------------------------------ start

if (-not (Test-Path (Join-Path $Root 'node_modules'))) {
    Say 'Installing the Tauri CLI (first run only)...'
    $p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', 'npm', 'install' -WorkingDirectory $Root -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw 'npm install failed. Is Node.js installed?' }
}

if ($Reset) {
    Remove-Custom 'icon'
    Remove-Custom 'tray'
    Remove-Custom 'icon-small'
    Say 'Your images were removed; using the built-in artwork.'
}

if ($AppImage) {
    Import-Image $AppImage 'icon'
    if (-not $TrayImage) {
        # A single image is used everywhere.
        Remove-Custom 'tray'
        Remove-Custom 'icon-small'
    }
}
if ($TrayImage) { Import-Image $TrayImage 'tray' }

$customApp = Find-Custom 'icon'
$app = $customApp
if (-not $app) { $app = Join-Path $Defaults 'icon.svg' }

$tray = Find-Custom 'tray'
if (-not $tray) { if ($customApp) { $tray = $customApp } else { $tray = Join-Path $Defaults 'tray.svg' } }

$small = Find-Custom 'icon-small'
if (-not $small) { if ($customApp) { $small = $customApp } else { $small = Join-Path $Defaults 'icon-small.svg' } }

foreach ($f in $app, $tray, $small) {
    if (-not (Test-Path $f)) { throw "Missing image: $f" }
}
Say "App image:   $app"
Say "Small image: $small"
Say "Tray image:  $tray"

$tmp = Join-Path ([IO.Path]::GetTempPath()) ('usbToggle-icons-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    # 1. The standard Tauri icon set (PNGs, icns, Store logos).
    Say 'Generating the app icon set...'
    Invoke-TauriIcon $app (Join-Path $tmp 'set') @()
    $keep = @('32x32.png', '128x128.png', '128x128@2x.png', 'icon.png', 'icon.icns', 'StoreLogo.png')
    Get-ChildItem (Join-Path $tmp 'set') -File |
        Where-Object { $keep -contains $_.Name -or $_.Name -like 'Square*Logo.png' } |
        Copy-Item -Destination $Icons -Force

    # 2. icon.ico with our own frames (the small ones can use simpler artwork).
    Say 'Building icon.ico...'
    Invoke-TauriIcon $app (Join-Path $tmp 'frames') $AppFrames
    Invoke-TauriIcon $small (Join-Path $tmp 'small') $SmallFrames
    $frameFiles = @()
    foreach ($s in $SmallFrames) { $frameFiles += Join-Path $tmp "small\${s}x${s}.png" }
    foreach ($s in $AppFrames) { $frameFiles += Join-Path $tmp "frames\${s}x${s}.png" }
    Write-Ico (Join-Path $Icons 'icon.ico') $frameFiles

    # 3. Tray icons, one file per size.
    Say 'Generating tray icons...'
    Invoke-TauriIcon $tray (Join-Path $tmp 'tray') $TraySizes
    New-Item -ItemType Directory -Path $TrayDir -Force | Out-Null
    foreach ($s in $TraySizes) {
        Copy-Item (Join-Path $tmp "tray\${s}x${s}.png") (Join-Path $TrayDir "$s.png") -Force
    }

    # 4. Logo in the window header.
    Copy-Item (Join-Path $tmp 'frames\256x256.png') (Join-Path $Ui 'logo.png') -Force
} finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

Say 'Done. Run build.bat to build the app with the new icons.'
Say 'If Windows still shows the old icon on the taskbar or desktop, unpin and re-pin the app (Windows caches icons).'
