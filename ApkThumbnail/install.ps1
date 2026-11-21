<#
.SYNOPSIS
    Installs the APK thumbnail provider: Explorer shows the app icon of
    .apk / .xapk / .apks / .apkm files.
.PARAMETER NoRestart
    Do not restart Explorer / clear the thumbnail cache afterwards.
#>
param([switch]$NoRestart, [switch]$Pause)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

if (Restart-Elevated $PSCommandPath @(if ($NoRestart) { '-NoRestart' })) { return }

try {
    foreach ($f in "$Fw64\csc.exe", "$Fw64\RegAsm.exe", "$Fw64\WPF\PresentationCore.dll") {
        if (-not (Test-Path $f)) { throw ".NET Framework 4.x component missing: $f" }
    }

    # 1. compile with the C# compiler that ships with Windows
    Write-Host 'Compiling ApkThumbnail.dll...'
    $buildDir = Join-Path $env:TEMP 'ApkThumbnail-build'
    New-Item -ItemType Directory -Force $buildDir | Out-Null
    $built = Join-Path $buildDir $DllName
    & "$Fw64\csc.exe" /nologo /target:library /optimize+ /out:"$built" /lib:"$Fw64\WPF" `
        /r:System.IO.Compression.dll /r:PresentationCore.dll /r:WindowsBase.dll /r:System.Xaml.dll `
        (Join-Path $PSScriptRoot 'ApkThumbnail.cs')
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }

    # 2. copy to Program Files (a loaded old copy can't be deleted, but it can be renamed)
    Stop-ThumbnailHosts
    New-Item -ItemType Directory -Force $InstallDir | Out-Null
    Get-ChildItem $InstallDir -Filter '*.old' | Remove-Item -Force -ErrorAction SilentlyContinue
    $target = Join-Path $InstallDir $DllName
    if (Test-Path $target) {
        try { Remove-Item $target -Force }
        catch { Rename-Item $target "$DllName.$([DateTime]::Now.Ticks).old" }
    }
    Copy-Item $built $target
    Remove-Item $buildDir -Recurse -Force -ErrorAction SilentlyContinue

    # 3. register the COM class for 64-bit and 32-bit processes (32-bit apps' file dialogs)
    Write-Host 'Registering COM server...'
    foreach ($fw in $Fw64, $Fw32) {
        if (-not (Test-Path "$fw\RegAsm.exe")) { continue }
        & "$fw\RegAsm.exe" /nologo /silent /codebase "$target" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "RegAsm failed ($fw)." }
    }

    # 4. drop ApkShellext2: its in-Explorer handlers keep .apk files open ("file in use")
    Remove-ApkShellExt

    # 5. hook the thumbnail handler to the file types
    Write-Host 'Registering thumbnail handler for' ($Extensions -join ', ')
    foreach ($ext in $Extensions) {
        Set-TrackedValue HKLM "Software\Classes\$ext\ShellEx\$ThumbIid" '' $Clsid
        Set-TrackedValue HKLM "Software\Classes\SystemFileAssociations\$ext\ShellEx\$ThumbIid" '' $Clsid
        Set-TrackedValue HKLM "Software\Classes\$ext\ShellEx\IconHandler" '' $IconClsid
        Set-TrackedValue HKLM "Software\Classes\SystemFileAssociations\$ext\ShellEx\IconHandler" '' $IconClsid
        # Explorer only asks the IconHandler when DefaultIcon is "%1" (Details / List / Small icons)
        Set-TrackedValue HKLM "Software\Classes\$ext\DefaultIcon" '' '%1'
        # no small "associated app" badge on top of the icon
        Set-TrackedValue HKLM "Software\Classes\$ext" 'TypeOverlay' ''
        Set-TrackedValue HKLM "Software\Classes\SystemFileAssociations\$ext" 'TypeOverlay' ''

        foreach ($id in Get-ProgIds $ext) {
            foreach ($hive in 'HKCU', 'HKLM') {
                $root = Get-RegRoot $hive
                $k = $root.OpenSubKey("Software\Classes\$id")
                if (-not $k) { continue }
                $k.Close()
                Set-TrackedValue $hive "Software\Classes\$id" 'TypeOverlay' ''
                # an app that registered its own (broken) thumbnail handler would win over ours
                $t = $root.OpenSubKey("Software\Classes\$id\ShellEx\$ThumbIid")
                if ($t) {
                    $t.Close()
                    Set-TrackedValue $hive "Software\Classes\$id\ShellEx\$ThumbIid" '' $Clsid
                }
            }
            foreach ($hive in 'HKCU', 'HKLM') {
                $root = Get-RegRoot $hive
                $k = $root.OpenSubKey("Software\Classes\$id")
                if ($k) {
                    $k.Close()
                    Set-TrackedValue $hive "Software\Classes\$id\ShellEx\IconHandler" '' $IconClsid
                    # an app's own DefaultIcon (e.g. MuMu's MumuApk.ico) would bypass the IconHandler
                    Set-TrackedValue $hive "Software\Classes\$id\DefaultIcon" '' '%1'
                }
            }
        }
    }
    Set-TrackedValue HKLM 'Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved' $Clsid 'APK Thumbnail Provider'
    Set-TrackedValue HKLM 'Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved' $IconClsid 'APK Icon Handler'

    Send-AssocChanged
    if (-not $NoRestart) { Restart-Explorer }

    Write-Host ''
    Write-Host "Installed to $InstallDir" -ForegroundColor Green
    Write-Host 'Icons show in every Explorer view, including Details, List and Small icons.'
}
catch {
    Write-Host "ERROR: $_" -ForegroundColor Red
}
finally {
    if ($Pause) { Read-Host 'Press Enter to close' | Out-Null }
}
