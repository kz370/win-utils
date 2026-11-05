@echo off
setlocal

cd /d "%~dp0"

echo ============================
echo       WIN UTILS RELEASE
echo ============================
echo.

rem --- Release date / default tag ---
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd"') do set "RELEASE_DATE=%%i"
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd"') do set "DATE_TAG=%%i"

set /p TAG=Enter release tag (default: win-utils-%DATE_TAG%):
if "%TAG%"=="" set "TAG=win-utils-%DATE_TAG%"

rem --- Prepare release-assets folder ---
if not exist "release-assets" mkdir "release-assets"
del /q "release-assets\*" 2>nul

echo Collecting built applications into release-assets...
echo.

rem Core standalone apps
if exist "cpulimiter\cpulimit.exe" copy /y "cpulimiter\cpulimit.exe" "release-assets\" >nul
if exist "f-delete\fdel.exe" copy /y "f-delete\fdel.exe" "release-assets\" >nul
if exist "restartAwkatSalat\RestartAwqat.exe" copy /y "restartAwkatSalat\RestartAwqat.exe" "release-assets\" >nul
if exist "SetDefaultApps\SetDefaultApps.exe" copy /y "SetDefaultApps\SetDefaultApps.exe" "release-assets\" >nul
if exist "SetDefaultApps\UserChoiceLatest.exe" copy /y "SetDefaultApps\UserChoiceLatest.exe" "release-assets\" >nul

rem usbToggle build outputs (installer + portable zip)
if exist "usbToggle\dist\*.exe" copy /y "usbToggle\dist\*.exe" "release-assets\" >nul
if exist "usbToggle\dist\*.zip" copy /y "usbToggle\dist\*.zip" "release-assets\" >nul

rem --- Verify we collected something ---
dir /b "release-assets" 2>nul | findstr /r "." >nul
if errorlevel 1 (
    echo No release files found in release-assets.
    echo Build the apps first, then run this script again.
    pause
    exit /b 1
)

echo Collected:
for %%F in ("release-assets\*") do echo   %%~nxF
echo.

echo Checking GitHub CLI...
gh api user --jq .login >nul 2>&1
if errorlevel 1 (
    echo GitHub CLI not authenticated with a working account.
    echo Please run: gh auth login -h github.com
    pause
    exit /b 1
)
for /f %%u in ('gh api user --jq .login') do echo Logged in as %%u

echo.
echo Pushing latest commits...
git push origin main
if errorlevel 1 goto :error

echo.
echo Creating GitHub release %TAG% (%RELEASE_DATE%)...
gh release create "%TAG%" --title "Win Utils %RELEASE_DATE%" --notes "Win Utils release dated %RELEASE_DATE%. Bundled apps: cpulimit, fdel, RestartAwqat, SetDefaultApps, usbToggle (0.1.0)." --target main
if errorlevel 1 goto :error

echo.
echo Uploading applications...
for %%F in ("release-assets\*") do (
    if exist "%%~fF" (
        echo Uploading %%~nxF...
        gh release upload "%TAG%" "%%~fF"
        if errorlevel 1 goto :error
    )
)

echo.
echo ============================
echo Release published successfully!
echo ============================
echo https://github.com/kz370/win-utils/releases/tag/%TAG%
echo.

pause
exit /b 0

:error
echo.
echo Something went wrong. Check the error above.
pause
exit /b 1