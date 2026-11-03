@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0

echo [usbToggle] Building Tauri Release...
cd /d "%ROOT%"
cargo tauri build 2>&1
if %errorlevel% neq 0 (
    echo [usbToggle] BUILD FAILED
    exit /b %errorlevel%
)

echo [usbToggle] DONE. Installer in src-tauri\target-release\release\msi
exit /b 0
