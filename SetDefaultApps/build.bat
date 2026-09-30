@echo off
setlocal
cd /d "%~dp0"
title Build SetDefaultApps.exe

set "OUT=SetDefaultApps.exe"

rem ===== Find the C# compiler that ships with Windows (.NET Framework 4.x) =====
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" goto :no_csc

rem ===== Check the source files =====
set "MISSING="
if not exist "SetDefaultApps.cs" set "MISSING=SetDefaultApps.cs"
if not exist "run.ps1" set "MISSING=run.ps1"
if not exist "apps.ini" set "MISSING=apps.ini"
if defined MISSING goto :missing

rem ===== Get PS-SFTA if it is not here yet =====
if exist "SFTA.ps1" goto :have_sfta
echo SFTA.ps1 not found - downloading PS-SFTA...
powershell -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -UseBasicParsing 'https://raw.githubusercontent.com/DanysysTeam/PS-SFTA/master/SFTA.ps1' -OutFile 'SFTA.ps1'"
if not exist "SFTA.ps1" goto :no_sfta
:have_sfta

rem ===== Pick up an icon (.ico) placed next to this script =====
set "ICON="
for %%I in (*.ico) do set "ICON=%%~fI"

if exist "%OUT%" del /q "%OUT%" >nul 2>&1

if defined ICON goto :build_icon

echo No .ico file found next to build.bat - building without a custom icon.
"%CSC%" /nologo /target:exe /platform:x64 /optimize+ /out:"%OUT%" /resource:SFTA.ps1,SFTA.ps1 /resource:run.ps1,run.ps1 /resource:apps.ini,apps.ini SetDefaultApps.cs
goto :built

:build_icon
echo Using icon: %ICON%
"%CSC%" /nologo /target:exe /platform:x64 /optimize+ /win32icon:"%ICON%" /out:"%OUT%" /resource:SFTA.ps1,SFTA.ps1 /resource:run.ps1,run.ps1 /resource:apps.ini,apps.ini SetDefaultApps.cs

:built
if errorlevel 1 goto :build_failed
if not exist "%OUT%" goto :build_failed

echo.
echo ============================================
echo  Built: %~dp0%OUT%
echo ============================================
echo If Explorer still shows the old icon, rename the exe or restart explorer.exe.
pause
exit /b 0

:no_csc
echo [ERROR] csc.exe not found. Enable ".NET Framework 4.8 Advanced Services" in Windows Features.
pause
exit /b 1

:missing
echo [ERROR] Missing file next to build.bat: %MISSING%
pause
exit /b 1

:no_sfta
echo [ERROR] Could not download SFTA.ps1. Download it from github.com/DanysysTeam/PS-SFTA and put it next to build.bat.
pause
exit /b 1

:build_failed
echo.
echo [ERROR] Build failed. Read the messages above.
echo If SetDefaultApps.exe is currently running, close it and try again.
pause
exit /b 1
