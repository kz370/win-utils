# Reads apps.ini and makes each listed app the default for its file types (via PS-SFTA).
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'SFTA.ps1')

$script:AllProgIds = $null
function Get-AllProgIds {
    if ($null -eq $script:AllProgIds) {
        $script:AllProgIds = @(Get-ChildItem 'Registry::HKEY_CLASSES_ROOT' -Name -ErrorAction SilentlyContinue)
    }
    return $script:AllProgIds
}

function Find-App($PathList) {
    foreach ($raw in @($PathList)) {
        $p = [Environment]::ExpandEnvironmentVariables($raw.Trim())
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }
    return $null
}

function Get-TypeMeta($Ext) {
    $meta = $null
    $extKey = Get-Item "Registry::HKEY_CLASSES_ROOT\.$Ext" -ErrorAction SilentlyContinue
    if ($extKey) {
        $nativeId = $extKey.GetValue('')
        if ($nativeId) {
            $pk = Get-Item "Registry::HKEY_CLASSES_ROOT\$nativeId" -ErrorAction SilentlyContinue
            if ($pk) {
                $ftn = $pk.GetValue('FriendlyTypeName')
                $name = $pk.GetValue('')
                if ($name -and $name.StartsWith('@')) { $name = $null }
                if (-not $name -and $ftn -and -not $ftn.StartsWith('@')) { $name = $ftn }
                $meta = [ordered]@{
                    Name            = $name
                    FriendlyTypeName = $ftn
                    PerceivedType   = $pk.GetValue('PerceivedType')
                    EditFlags       = $pk.GetValue('EditFlags')
                }
            }
        }
    }
    return $meta
}

function Get-TypeName($Ext, $PerceivedType) {
    $word = switch ($PerceivedType) {
        'image'   { 'Image' }
        'audio'   { 'Audio File' }
        'video'   { 'Video' }
        'text'    { 'Document' }
        'archive' { 'Compressed Folder' }
        default   { 'File' }
    }
    return ($Ext.ToUpper() + ' ' + $word)
}

function Register-ProgId($Id, $Description, $Exe, $Meta, $PerceivedType, $TypeName) {
    $base = "HKCU:\Software\Classes\$Id"
    New-Item -Path $base -Force | Out-Null

    $name = $Description
    if ($Meta -and $Meta.Name) { $name = $Meta.Name }
    if ($TypeName) { $name = $TypeName }
    Set-ItemProperty -Path $base -Name '(default)' -Value $name

    if ($Meta -and $Meta.FriendlyTypeName) {
        Set-ItemProperty -Path $base -Name 'FriendlyTypeName' -Value $Meta.FriendlyTypeName
    }
    $pt = $null
    if ($Meta -and $Meta.PerceivedType) { $pt = $Meta.PerceivedType }
    if (-not $pt -and $PerceivedType) { $pt = $PerceivedType }
    if ($pt) { Set-ItemProperty -Path $base -Name 'PerceivedType' -Value $pt }
    if ($Meta -and $Meta.EditFlags) {
        Set-ItemProperty -Path $base -Name 'EditFlags' -Value $Meta.EditFlags -Type DWord
    }
    Set-ItemProperty -Path $base -Name 'InfoTip' -Value 'prop:System.ItemType;System.Size;System.DateModified'

    New-Item -Path "$base\DefaultIcon" -Force | Out-Null
    Set-ItemProperty -Path "$base\DefaultIcon" -Name '(default)' -Value "`"$Exe`",0"
    New-Item -Path "$base\shell\open\command" -Force | Out-Null
    Set-ItemProperty -Path "$base\shell\open\command" -Name '(default)' -Value "`"$Exe`" `"%1`""
}

function Get-NativeMap($Regex, $Exclude) {
    $map = @{}
    foreach ($n in (Get-AllProgIds | Where-Object { $_ -match $Regex -and $_ -ne $Exclude } | Sort-Object)) {
        $dot = $n.IndexOf('.')
        if ($dot -ge 0) { $map[$n.Substring($dot + 1).ToLower()] = $n }
    }
    return $map
}

function Apply-App($Name, $ProgId, $Description, $ExePaths, $Extensions, $NativeRegex, $PerceivedType) {
    Write-Host ""
    Write-Host "=== $Name ===" -ForegroundColor Cyan

    $exe = Find-App $ExePaths
    if (-not $exe) {
        Write-Warning "$Name was not found. Skipping."
        return
    }
    Write-Host "Using: $exe"

    if ($ProgId) { Register-ProgId $ProgId $Description $exe $null $PerceivedType $Description }

    $native = $null
    if ($NativeRegex) { $native = Get-NativeMap $NativeRegex $ProgId }

    $changed = 0; $skipped = 0; $failed = 0; $nativeUsed = 0; $typedUsed = 0
    foreach ($token in ($Extensions -split '\s+')) {
        if (-not $token) { continue }

        $ext = $token
        $explicit = $null
        if ($token.Contains('=')) {
            $parts = $token.Split('=')
            $ext = $parts[0]
            $explicit = $parts[1]
        }
        $ext = $ext.TrimStart('.').ToLower()
        $x = ".$ext"

        $target = $ProgId
        $meta = $null
        if ($explicit) {
            $target = $explicit
        } elseif (($null -ne $native) -and $native.ContainsKey($ext)) {
            $target = $native[$ext]
            $nativeUsed++
        } else {
            $meta = Get-TypeMeta $ext
            if ($meta -or $PerceivedType) {
                $target = "$ProgId.$ext"
                $typedUsed++
            }
        }
        if (-not $target) {
            Write-Warning "No ProgID for $x (set a generic ProgId or use ext=ProgId)"
            $failed++
            continue
        }

        if ($target -eq "$ProgId.$ext") {
            $typeName = $null
            if ($meta -and $meta.Name) { $typeName = $meta.Name }
            if (-not $typeName) { $typeName = Get-TypeName $ext $PerceivedType }
            Register-ProgId $target $Description $exe $meta $PerceivedType $typeName
        }

        try {
            if ((Get-FTA $x) -eq $target) {
                $skipped++
            } else {
                Set-FTA $target $x
                Write-Host "Set: $x -> $target"
                $changed++
            }
        } catch {
            Write-Warning "Failed: $x"
            $failed++
        }
    }

    Write-Host "Changed: $changed   Skipped (already set): $skipped   Failed: $failed"
    if ($typedUsed -gt 0) {
        Write-Host "Extensions using type-aware ProgIDs (correct tooltip type): $typedUsed"
    }
    if ($NativeRegex) {
        Write-Host "Extensions using the app's own ProgID (own icons): $nativeUsed"
        if ($nativeUsed -eq 0) {
            Write-Warning "No ProgIDs registered by $Name itself were found, so the generic icon is used."
            Write-Host "Tip: in $Name's settings, register its file types first, then run this again."
        }
    }
}

# ===== Read apps.ini =====
function Read-AppsIni($Path) {
    $apps = @()
    $cur = $null
    foreach ($raw in (Get-Content -LiteralPath $Path -Encoding UTF8)) {
        $line = $raw.Trim()
        if ($line -eq '' -or $line.StartsWith('#') -or $line.StartsWith(';')) { continue }

        if ($line -match '^\[(.+)\]$') {
            $cur = [pscustomobject]@{
                Name = $Matches[1].Trim(); Enabled = $true; Exe = @(); Plain = @()
                Explicit = [ordered]@{}; ProgId = ''; IconPrefix = ''; PerceivedType = ''
            }
            $apps += $cur
            continue
        }
        if ($null -eq $cur) { Write-Warning "Ignoring line outside a [section]: $line"; continue }

        $eq = $line.IndexOf('=')
        if ($eq -lt 1) { Write-Warning "Ignoring line (no '=' found): $line"; continue }
        $key = $line.Substring(0, $eq).Trim().ToLower()
        $val = $line.Substring($eq + 1).Trim()

        switch ($key) {
            'enabled'     { $cur.Enabled = ($val -match '^(yes|true|1|on)$') }
            'exe'         { if ($val) { $cur.Exe += $val } }
            'extensions'  { $cur.Plain += @($val -split '[\s,]+' | Where-Object { $_ }) }
            'progid'      { $cur.ProgId = $val }
            'icon-prefix' { $cur.IconPrefix = $val }
            'perceived-type' { $cur.PerceivedType = $val }
            default       { $cur.Explicit[$key.TrimStart('.')] = $val }
        }
    }
    return $apps
}

# ===== Main =====
$config = Join-Path $PSScriptRoot 'apps.ini'
if (-not (Test-Path -LiteralPath $config)) {
    Write-Warning "apps.ini not found."
    return
}

foreach ($a in @(Read-AppsIni $config)) {
    if (-not $a.Enabled) {
        Write-Host ""
        Write-Host "=== $($a.Name) === (enabled = no, skipped)" -ForegroundColor DarkGray
        continue
    }

    $tokens = @($a.Plain)
    foreach ($k in $a.Explicit.Keys) { $tokens += "$k=$($a.Explicit[$k])" }

    # A generic ProgID is only needed for plain extensions
    $progId = ''
    if ($a.Plain.Count -gt 0) {
        $progId = $a.ProgId
        if (-not $progId) { $progId = 'SetDefaultApps.' + ($a.Name -replace '[^A-Za-z0-9]', '') }
    }

    $regex = ''
    if ($a.IconPrefix) { $regex = '^' + [regex]::Escape($a.IconPrefix) + '[^.]*\.[^.]+$' }

    Apply-App $a.Name $progId "$($a.Name) file" $a.Exe ($tokens -join ' ') $regex $a.PerceivedType
}

try { & ie4uinit.exe -show } catch { }
Write-Host ""
Write-Host "All done. Sign out/in or restart explorer.exe if icons don't refresh."
