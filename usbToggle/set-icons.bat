@echo off
setlocal
cd /d "%~dp0"

rem Replaces the app icons in the assets folder from a PNG or SVG image:
rem   assets\app_icon.ico     window, taskbar, Start menu, .exe, installer
rem   assets\tray_icon.ico    tray icon (next to the clock)
rem   assets\header_logo.png  logo at the top of the app window
rem
rem   set-icons.bat my-icon.png          one image for everything (or drag it onto this file)
rem   set-icons.bat app.png tray.png     a separate image for the tray
rem   set-icons.bat --reset              back to the built-in icons
rem   set-icons.bat                      shows this help, changes nothing
rem
rem You can also skip this and copy your own .ico files into assets\ with those names.
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
