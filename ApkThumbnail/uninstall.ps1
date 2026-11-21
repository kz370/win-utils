<#
.SYNOPSIS
    Removes the APK thumbnail provider and restores every registry value install.ps1 changed.
.PARAMETER NoRestart
    Do not restart Explorer / clear the thumbnail cache afterwards.
#>
param([switch]$NoRestart, [switch]$Pause)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

if (Restart-Elevated $PSCommandPath @(if ($NoRestart) { '-NoRestart' })) { return }

try {
    Stop-ThumbnailHosts

    $target = Join-Path $InstallDir $DllName
    if (Test-Path $target) {
        foreach ($fw in $Fw64, $Fw32) {
            if (Test-Path "$fw\RegAsm.exe") { & "$fw\RegAsm.exe" /nologo /silent /unregister "$target" | Out-Null }
        }
    }
    # in case the DLL was already gone
    foreach ($id in $Clsid, $IconClsid) {
        foreach ($p in "Software\Classes\CLSID\$id", "Software\Classes\WOW6432Node\CLSID\$id") {
            [Microsoft.Win32.Registry]::LocalMachine.DeleteSubKeyTree($p, $false)
        }
    }
    foreach ($p in "Software\Classes\$ProgId", "Software\Classes\$IconProgId") {
        [Microsoft.Win32.Registry]::LocalMachine.DeleteSubKeyTree($p, $false)
    }

    Restore-TrackedValues
    Send-AssocChanged
    if (-not $NoRestart) { Restart-Explorer }

    Stop-ThumbnailHosts
    try { Remove-Item $InstallDir -Recurse -Force }
    catch { Write-Warning "Could not delete $InstallDir (file in use). Delete it after a reboot." }

    Write-Host 'Uninstalled.' -ForegroundColor Green
}
catch {
    Write-Host "ERROR: $_" -ForegroundColor Red
}
finally {
    if ($Pause) { Read-Host 'Press Enter to close' | Out-Null }
}
