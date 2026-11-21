# ApkThumbnailProvider

A lightweight Windows Explorer thumbnail and file icon provider for Android package files.

It restores app icons for `.apk`, `.xapk`, `.apks`, and `.apkm` files on Windows 11 by registering a native COM thumbnail and file icon handler. It is intended for systems where ApkShellExt2 no longer displays package icons correctly after recent Windows updates.

## Features

- Shows Android app icons in Windows Explorer thumbnails and in Details, List and Small icons views
- Supports APK and common split/bundle formats
- Handles raster, adaptive, vector, and density-specific Android icons
- Registers both 64-bit and 32-bit COM handlers
- Restores previous file association settings when uninstalled
- Does not load the thumbnail provider directly into `explorer.exe`

## Requirements

- Windows 11
- .NET Framework 4.x components included with Windows
- Administrator permissions for installation

## Install

Right-click `Install.cmd` and choose **Run as administrator**.

The installer compiles `ApkThumbnail.dll`, registers it for Explorer, updates the relevant file associations, and restarts Explorer to refresh the thumbnail cache.

To install without restarting Explorer automatically:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\install.ps1 -NoRestart
```

The installer also unregisters ApkShellExt2 if it finds it. Its handlers run inside `explorer.exe` and keep `.apk` files open, so Explorer reports them as in use and they can't be deleted. ApkShellExt2 is not restored on uninstall.

If another APK thumbnail or icon handler is installed, uninstall it first if Explorer continues showing blank icons.

## Uninstall

Right-click `Uninstall.cmd` and choose **Run as administrator**.

The uninstall script removes this provider and restores the registry values that existed before installation.

## Project name

Recommended repository name: **ApkThumbnailProvider**

It is concise, searchable, and describes the actual Windows integration more accurately than `ApkThumbnail` alone.
