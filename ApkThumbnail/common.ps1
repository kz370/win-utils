# Shared settings and helpers for install.ps1 / uninstall.ps1.

$Clsid      = '{5E2A7C4B-9D31-4F6A-B8E2-3C7D1A0F9B64}'   # must match the Guid in ApkThumbnail.cs
$IconClsid  = '{F0AEA457-4A09-494F-8240-E59F6F225285}'   # ApkIconHandler: never takes a stream, see ApkThumbnail.cs
$ProgId     = 'ApkThumbnail.Provider'
$IconProgId = 'ApkThumbnail.IconHandler'
$ThumbIid   = '{e357fccd-a995-4576-b01f-234630154e96}'   # IThumbnailProvider
$Extensions = '.apk', '.xapk', '.apks', '.apkm'
$InstallDir = Join-Path $env:ProgramFiles 'ApkThumbnail'
$DllName    = 'ApkThumbnail.dll'
$StateRoot  = 'SOFTWARE\ApkThumbnail'
$StateKey   = "$StateRoot\Changes"
$NoneMarker = '<none>'
$Fw64       = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319"
$Fw32       = "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319"

# Relaunches the calling script elevated. Returns $true when the caller should exit.
function Restart-Elevated([string]$Script, [string[]]$ExtraArgs) {
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { return $false }
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$Script`"", '-Pause') + $ExtraArgs
    Start-Process powershell.exe -Verb RunAs -ArgumentList $argList
    return $true
}

function Get-RegRoot([string]$Hive) {
    if ($Hive -eq 'HKCU') { [Microsoft.Win32.Registry]::CurrentUser } else { [Microsoft.Win32.Registry]::LocalMachine }
}

# Sets a registry value and remembers what was there before, so uninstall can put it back.
function Set-TrackedValue([string]$Hive, [string]$Path, [string]$Name, [string]$Value) {
    $key = (Get-RegRoot $Hive).CreateSubKey($Path)
    $log = [Microsoft.Win32.Registry]::LocalMachine.CreateSubKey($StateKey)
    try {
        $id = "$Hive|$Path|$Name"
        if ($null -eq $log.GetValue($id)) {
            $old = $key.GetValue($Name, $null, 'DoNotExpandEnvironmentNames')
            $log.SetValue($id, $(if ($null -eq $old) { $NoneMarker } else { [string]$old }))
        }
        $key.SetValue($Name, $Value)
    }
    finally {
        $key.Close()
        $log.Close()
    }
}

# Deletes keys left empty by uninstall, walking upwards but never removing ...\Classes\<name> itself.
function Remove-EmptyKeys($Root, [string]$Path) {
    while ($Path -match '\\' -and $Path -notmatch '^Software\\Classes\\[^\\]+$') {
        $k = $Root.OpenSubKey($Path)
        if ($k) {
            $empty = $k.ValueCount -eq 0 -and $k.SubKeyCount -eq 0
            $k.Close()
            if (-not $empty) { break }
            $Root.DeleteSubKey($Path, $false)
        }
        $Path = $Path.Substring(0, $Path.LastIndexOf('\'))
    }
}

function Restore-TrackedValues {
    $log = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($StateKey)
    if ($log) {
        $entries = foreach ($n in $log.GetValueNames()) { [pscustomobject]@{ Id = $n; Old = [string]$log.GetValue($n) } }
        $log.Close()
        foreach ($e in $entries) {
            $hive, $path, $name = $e.Id -split '\|', 3
            $root = Get-RegRoot $hive
            $key = $root.OpenSubKey($path, $true)
            if (-not $key) { continue }
            if ($e.Old -eq $NoneMarker) { $key.DeleteValue($name, $false) } else { $key.SetValue($name, $e.Old) }
            $key.Close()
            Remove-EmptyKeys $root $path
        }
    }
    [Microsoft.Win32.Registry]::LocalMachine.DeleteSubKeyTree($StateRoot, $false)
}

# ProgIDs the extension is currently associated with (machine default, per-user default, user's choice).
function Get-ProgIds([string]$Ext) {
    $ids = @()
    foreach ($root in [Microsoft.Win32.Registry]::LocalMachine, [Microsoft.Win32.Registry]::CurrentUser) {
        $k = $root.OpenSubKey("Software\Classes\$Ext")
        if ($k) { $v = $k.GetValue(''); if ($v) { $ids += [string]$v }; $k.Close() }
    }
    $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$Ext\UserChoice")
    if ($k) { $v = $k.GetValue('ProgId'); if ($v) { $ids += [string]$v }; $k.Close() }
    $ids | Select-Object -Unique
}

# ApkShellext2 registers in-process handlers (context menu, info tip, icon, thumbnail) that run inside
# explorer.exe and keep .apk files open, so Explorer reports them "in use" and they can't be deleted.
# Unregister it and remove any shell keys that still point at its classes.
function Remove-ApkShellExt {
    $found = @{}
    foreach ($clsidPath in 'Software\Classes\CLSID', 'Software\Classes\WOW6432Node\CLSID') {
        $clsids = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($clsidPath)
        if (-not $clsids) { continue }
        foreach ($id in $clsids.GetSubKeyNames()) {
            $k = $clsids.OpenSubKey("$id\InprocServer32")
            if (-not $k) { continue }
            $class = [string]$k.GetValue('Class')
            $codeBase = [string]$k.GetValue('CodeBase')
            $k.Close()
            if ($class -match '^ApkShellext') { $found[$id.ToLowerInvariant()] = $codeBase }
        }
        $clsids.Close()
    }
    if ($found.Count -eq 0) { return }
    Write-Host 'Removing ApkShellext2 (it keeps .apk files locked inside Explorer)...'

    # its own unregister code removes most of its shell keys
    foreach ($codeBase in $found.Values | Where-Object { $_ } | Select-Object -Unique) {
        $dll = try { ([Uri]$codeBase).LocalPath } catch { $null }
        if (-not $dll -or -not (Test-Path $dll)) { continue }
        foreach ($fw in $Fw64, $Fw32) {
            if (Test-Path "$fw\RegAsm.exe") { & "$fw\RegAsm.exe" /nologo /silent /unregister "$dll" | Out-Null }
        }
    }

    # leftovers: ShellEx entries (on the extensions and their ProgIDs) that still name its classes
    $names = @()
    foreach ($ext in $Extensions) { $names += $ext, "SystemFileAssociations\$ext"; $names += Get-ProgIds $ext }
    foreach ($root in [Microsoft.Win32.Registry]::LocalMachine, [Microsoft.Win32.Registry]::CurrentUser) {
        foreach ($name in $names | Select-Object -Unique) {
            Remove-ShellExRefs $root "Software\Classes\$name\ShellEx" $found.Keys
        }
    }
    foreach ($id in $found.Keys) {
        foreach ($clsidPath in 'Software\Classes\CLSID', 'Software\Classes\WOW6432Node\CLSID') {
            [Microsoft.Win32.Registry]::LocalMachine.DeleteSubKeyTree("$clsidPath\$id", $false)
        }
        $approved = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved', $true)
        if ($approved) {
            foreach ($v in $approved.GetValueNames()) { if ($v.ToLowerInvariant() -eq $id) { $approved.DeleteValue($v, $false) } }
            $approved.Close()
        }
    }
}

# Deletes every subkey under $Path whose default value is one of $Clsids (lower-case).
function Remove-ShellExRefs($Root, [string]$Path, $Clsids) {
    $k = $Root.OpenSubKey($Path)
    if (-not $k) { return }
    $subs = $k.GetSubKeyNames()
    $k.Close()
    foreach ($s in $subs) {
        $sub = $Root.OpenSubKey("$Path\$s")
        if (-not $sub) { continue }
        $v = ([string]$sub.GetValue('')).ToLowerInvariant()
        $sub.Close()
        if ($Clsids -contains $v) { $Root.DeleteSubKeyTree("$Path\$s", $false) }
        else { Remove-ShellExRefs $Root "$Path\$s" $Clsids }
    }
}

# Thumbnail handlers run in dllhost.exe surrogates; kill them so the DLL is not locked.
function Stop-ThumbnailHosts {
    Get-CimInstance Win32_Process -Filter "Name='dllhost.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -match 'AB8902B4-09CA-4BB6-B78D-A8F59079A8D5' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

function Send-AssocChanged {
    if (-not ('ApkThumb.Shell' -as [type])) {
        Add-Type -Namespace ApkThumb -Name Shell -MemberDefinition @'
[DllImport("shell32.dll")] public static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);
'@
    }
    [ApkThumb.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)   # SHCNE_ASSOCCHANGED
}

function Restart-Explorer {
    Write-Host 'Restarting Explorer and clearing the thumbnail cache...'
    Get-Process explorer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 700
    Get-ChildItem "$env:LOCALAPPDATA\Microsoft\Windows\Explorer" -Filter 'thumbcache_*.db' -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Get-ChildItem "$env:LOCALAPPDATA\Microsoft\Windows\Explorer" -Filter 'iconcache_*.db' -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Remove-Item "$env:LOCALAPPDATA\IconCache.db" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }
}
