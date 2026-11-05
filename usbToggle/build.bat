@echo off
setlocal
cd /d "%~dp0"

where cargo >nul 2>nul || (echo [usbToggle] Rust is required: https://rustup.rs & exit /b 1)
where npm >nul 2>nul || (echo [usbToggle] Node.js is required: https://nodejs.org & exit /b 1)

if not exist node_modules (
    echo [usbToggle] Installing the Tauri CLI...
    call npm install || exit /b 1
)

echo [usbToggle] Building release...
call npx tauri build
if errorlevel 1 (
    echo [usbToggle] BUILD FAILED
    exit /b 1
)

copy /y "src-tauri\target\release\usbToggle.exe" "usbToggle.exe" >nul
echo.
echo [usbToggle] Done.
echo   App:        %CD%\usbToggle.exe
echo   Installers: %CD%\src-tauri\target\release\bundle\
exit /b 0
