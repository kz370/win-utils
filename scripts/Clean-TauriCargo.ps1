<#
.SYNOPSIS
    Finds Tauri apps on a whole drive/partition and cleans their Cargo build artifacts (target folders).

.DESCRIPTION
    A folder counts as a Tauri project when it contains Cargo.toml AND one of
    tauri.conf.json / tauri.conf.json5 / Tauri.toml (normally "src-tauri").
    If the project is part of a Cargo workspace, the workspace's target folder is used.

.PARAMETER Root
    Drive or folder to scan. Default: C:\

.PARAMETER Exclude
    Folders to skip. Each entry can be:
      - a full path          -> D:\Backups\old-apps   (skips that folder and everything under it)
      - a wildcard path      -> D:\Work\*\archive
      - a bare folder name   -> legacy                (skips any folder with that name, wildcards allowed)

.PARAMETER UseCargoClean
    Run `cargo clean --offline` per project instead of deleting the target folder directly.
    (Handles custom target-dir configs; needs cargo in PATH.)

.PARAMETER DryRun
    Only list what would be cleaned and how much space it would free.

.PARAMETER Yes
    Skip the confirmation prompt.

.EXAMPLE
    .\Clean-TauriCargo.ps1 -Root D:\ -DryRun

.EXAMPLE
    .\Clean-TauriCargo.ps1 -Root C:\ -Exclude "C:\Users\me\work\keep-builds","legacy","D:\Projects\*\release"

.EXAMPLE
    .\Clean-TauriCargo.ps1 -Root E:\ -UseCargoClean -Yes
#>
[CmdletBinding()]
param(
    [string]$Root = 'C:\',
    [string[]]$Exclude = @(),
    [switch]$UseCargoClean,
    [switch]$DryRun,
    [switch]$Yes
)

# Folders never descended into (speeds up the scan a lot). Edit as needed.
$SkipNames = @(
    'node_modules', 'target', '.git', '.cargo', '.rustup',
    '$Recycle.Bin', 'System Volume Information',
    'Windows', 'Program Files', 'Program Files (x86)', 'ProgramData', 'AppData'
)
$TauriConfigs = @('tauri.conf.json', 'tauri.conf.json5', 'Tauri.toml')

$Exclude = @($Exclude | ForEach-Object { $_.Trim().Replace('/', '\').TrimEnd('\') } | Where-Object { $_ })

function Test-Excluded([string]$path) {
    foreach ($e in $Exclude) {
        if ($e -match '[\\:]') {
            # path-style exclusion
            if ($e -match '[\*\?]') {
                if ($path -like $e -or $path -like "$e\*") { return $true }
            }
            elseif ($path -eq $e -or $path.StartsWith($e + '\', [StringComparison]::OrdinalIgnoreCase)) {
                return $true
            }
        }
        else {
            # bare folder-name exclusion
            if ((Split-Path $path -Leaf) -like $e) { return $true }
        }
    }
    return $false
}

function Test-IsTauriProject([string]$dir) {
    if (-not [IO.File]::Exists((Join-Path $dir 'Cargo.toml'))) { return $false }
    foreach ($c in $TauriConfigs) {
        if ([IO.File]::Exists((Join-Path $dir $c))) { return $true }
    }
    return $false
}

function Find-TauriProjects([string]$start) {
    $stack = [System.Collections.Generic.Stack[string]]::new()
    $stack.Push($start)
    while ($stack.Count -gt 0) {
        $dir = $stack.Pop()
        if (Test-Excluded $dir) { continue }
        if (Test-IsTauriProject $dir) { $dir }

        try { $subs = @([IO.Directory]::EnumerateDirectories($dir)) } catch { continue }
        foreach ($s in $subs) {
            if ($SkipNames -contains [IO.Path]::GetFileName($s)) { continue }
            try { $attr = [IO.File]::GetAttributes($s) } catch { continue }
            if ($attr -band [IO.FileAttributes]::ReparsePoint) { continue }  # skip junctions/symlinks
            $stack.Push($s)
        }
    }
}

# Nearest ancestor (or the project itself) that declares [workspace]; otherwise the project dir.
function Get-CargoRoot([string]$projDir) {
    $cur = $projDir
    while ($cur) {
        $manifest = Join-Path $cur 'Cargo.toml'
        if ([IO.File]::Exists($manifest) -and (Select-String -LiteralPath $manifest -Pattern '^\s*\[workspace\]' -Quiet)) {
            return $cur
        }
        $parent = [IO.Directory]::GetParent($cur)
        $cur = if ($parent) { $parent.FullName } else { $null }
    }
    return $projDir
}

function Get-DirSize([string]$path) {
    $sum = 0L
    try {
        foreach ($f in ([IO.DirectoryInfo]$path).EnumerateFiles('*', [IO.SearchOption]::AllDirectories)) {
            $sum += $f.Length
        }
    } catch { }
    return $sum
}

function Format-Size([long]$bytes) {
    if ($bytes -ge 1GB) { return '{0:N2} GB' -f ($bytes / 1GB) }
    if ($bytes -ge 1MB) { return '{0:N1} MB' -f ($bytes / 1MB) }
    return '{0:N0} KB' -f ($bytes / 1KB)
}

# ---------------------------------------------------------------- main
if (-not (Test-Path -LiteralPath $Root)) { throw "Root path not found: $Root" }
$Root = (Resolve-Path -LiteralPath $Root).Path

if ($UseCargoClean -and -not (Get-Command cargo -ErrorAction SilentlyContinue)) {
    throw 'cargo was not found in PATH. Install Rust or run without -UseCargoClean.'
}

Write-Host "Scanning $Root for Tauri projects..." -ForegroundColor Cyan
if ($Exclude.Count) { Write-Host ("Excluding: " + ($Exclude -join ', ')) -ForegroundColor DarkGray }

$items = @{}   # key = cargo root, value = @{ Project; Target; Size }
Find-TauriProjects $Root | ForEach-Object {
    $proj = $_
    $cargoRoot = Get-CargoRoot $proj
    if ($items.ContainsKey($cargoRoot)) { return }

    $target = Join-Path $cargoRoot 'target'
    if (Test-Excluded $cargoRoot) { return }
    if (-not $UseCargoClean -and -not (Test-Path -LiteralPath $target -PathType Container)) { return }

    $size = if (Test-Path -LiteralPath $target -PathType Container) { Get-DirSize $target } else { 0L }
    $items[$cargoRoot] = [pscustomobject]@{
        Project = $proj
        Root    = $cargoRoot
        Target  = $target
        Size    = $size
    }
    Write-Host ("  found: {0}  [{1}]" -f $proj, (Format-Size $size))
}

if ($items.Count -eq 0) {
    Write-Host 'No Tauri projects with build artifacts found.' -ForegroundColor Yellow
    return
}

$list  = $items.Values | Sort-Object Size -Descending
$total = ($list | Measure-Object Size -Sum).Sum

Write-Host ''
$list | ForEach-Object { '{0,12}  {1}' -f (Format-Size $_.Size), $_.Root } | Write-Host
Write-Host ('-' * 60)
Write-Host ("{0} project(s), {1} reclaimable" -f $list.Count, (Format-Size $total)) -ForegroundColor Green

if ($DryRun) {
    Write-Host 'Dry run: nothing was deleted.' -ForegroundColor Yellow
    return
}

if (-not $Yes) {
    $answer = Read-Host 'Clean all of the above? (y/N)'
    if ($answer -notmatch '^(y|yes)$') { Write-Host 'Aborted.'; return }
}

$freed = 0L
foreach ($it in $list) {
    Write-Host ("Cleaning {0} ..." -f $it.Root) -NoNewline
    try {
        if ($UseCargoClean) {
            & cargo clean --offline --manifest-path (Join-Path $it.Project 'Cargo.toml') 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "cargo clean exited with code $LASTEXITCODE" }
        }
        else {
            try { Remove-Item -LiteralPath $it.Target -Recurse -Force -ErrorAction Stop }
            catch { cmd /c "rmdir /s /q `"$($it.Target)`"" 2>&1 | Out-Null }   # fallback for long paths
            if (Test-Path -LiteralPath $it.Target) { throw 'some files could not be removed (in use?)' }
        }
        $freed += $it.Size
        Write-Host ' done' -ForegroundColor Green
    }
    catch {
        Write-Host (" failed: {0}" -f $_.Exception.Message) -ForegroundColor Red
    }
}

Write-Host ''
Write-Host ("Freed about {0}" -f (Format-Size $freed)) -ForegroundColor Green