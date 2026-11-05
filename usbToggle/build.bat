@echo off
setlocal
cd /d "%~dp0"

rem Builds usbToggle.exe, then in dist\:
rem   usbToggle-<version>-setup.exe      Inno Setup installer (needs Inno Setup 6.3+)
rem   usbToggle-<version>-portable.zip   portable version (no install)

where cargo >nul 2>nul || (echo [usbToggle] Rust is required: https://rustup.rs & exit /b 1)
where npm >nul 2>nul || (echo [usbToggle] Node.js is required: https://nodejs.org & exit /b 1)

set "VERSION="
for /f "usebackq delims=" %%v in (`powershell -NoProfile -Command "(Get-Content -Raw 'src-tauri\tauri.conf.json' | ConvertFrom-Json).version"`) do set "VERSION=%%v"
if not defined VERSION (
    echo [usbToggle] Could not read the version from src-tauri\tauri.conf.json
    exit /b 1
)

if not exist node_modules (
    echo [usbToggle] Installing the Tauri CLI...
    call npm install || exit /b 1
)

echo [usbToggle] Building usbToggle %VERSION%...
call npx tauri build --no-bundle
if errorlevel 1 (
    echo [usbToggle] BUILD FAILED
    exit /b 1
)

set "EXE=%CD%\src-tauri\target\release\usbToggle.exe"
set "DIST=%CD%\dist"
if not exist "%DIST%" mkdir "%DIST%"

rem ---------------------------------------------------------------- portable
echo [usbToggle] Packing the portable version...
set "PORTABLE=%DIST%\usbToggle-%VERSION%-portable"
if exist "%PORTABLE%" rmdir /s /q "%PORTABLE%"
mkdir "%PORTABLE%"
copy /y "%EXE%" "%PORTABLE%\usbToggle.exe" >nul || exit /b 1
copy /y "installer\portable.txt" "%PORTABLE%\portable.txt" >nul || exit /b 1
powershell -NoProfile -Command "Compress-Archive -Path '%PORTABLE%' -DestinationPath '%PORTABLE%.zip' -Force"
if errorlevel 1 (
    echo [usbToggle] Could not create the portable zip
    exit /b 1
)

rem --------------------------------------------------------------- installer
set "ISCC="
for %%I in (ISCC.exe) do set "ISCC=%%~$PATH:I"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC goto :no_inno

echo [usbToggle] Building the installer...
"%ISCC%" /Q "/DAppVersion=%VERSION%" "/DSourceExe=%EXE%" "/DOutputDir=%DIST%" "installer\usbToggle.iss"
if errorlevel 1 (
    echo [usbToggle] INSTALLER BUILD FAILED
    exit /b 1
)
goto :done

:no_inno
echo.
echo [usbToggle] Inno Setup 6 was not found, so no installer was built.
echo             Install it with:  winget install JRSoftware.InnoSetup
echo             then run build.bat again.

:done
echo.
echo [usbToggle] Done. Output in %DIST%
if exist "%DIST%\usbToggle-%VERSION%-setup.exe" echo   Installer: usbToggle-%VERSION%-setup.exe
echo   Portable:  usbToggle-%VERSION%-portable.zip  (and the unpacked folder)
exit /b 0
