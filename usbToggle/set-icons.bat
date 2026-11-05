@echo off
setlocal
cd /d "%~dp0"

rem Changes every USB Toggle icon: app window, taskbar, Start menu, tray,
rem installer, exe and the logo in the window header.
rem
rem   set-icons.bat                     regenerate from src-tauri\icons\source\
rem   set-icons.bat my-icon.png         one image for everything (or drag it onto this file)
rem   set-icons.bat app.png tray.png    a separate image for the tray
rem   set-icons.bat --reset             back to the built-in artwork
rem
rem Images: square PNG (512x512 or larger) or SVG with a transparent background.
rem Afterwards, run build.bat to build the app with the new icons.

set "PSARGS="
if /i "%~1"=="--reset" (
    set "PSARGS=-Reset"
    goto :run
)
if not "%~1"=="" set PSARGS=-AppImage "%~1"
if not "%~2"=="" set PSARGS=%PSARGS% -TrayImage "%~2"

:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\set-icons.ps1" %PSARGS%
if errorlevel 1 (
    echo.
    echo [icons] FAILED - see the message above.
    pause
    exit /b 1
)
echo.
pause
exit /b 0
