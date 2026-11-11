@echo off
setlocal

pushd "%~dp0"

set "DEST=%~1"
if "%DEST%"=="" set "DEST=%LOCALAPPDATA%\Programs\screenBlank"

if not exist "screenBlank.exe" (
    echo screenBlank.exe not found, building it first...
    call build.bat || goto :fail
)

rem Stop a running copy so the exe can be replaced.
if exist "%DEST%\screenBlank.exe" "%DEST%\screenBlank.exe" exit >nul 2>&1
timeout /t 1 /nobreak >nul

if not exist "%DEST%" mkdir "%DEST%" || goto :fail
copy /y "screenBlank.exe" "%DEST%\screenBlank.exe" >nul || goto :fail

set "LNK=%APPDATA%\Microsoft\Windows\Start Menu\Programs\screenBlank.lnk"
powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut($env:LNK); $s.TargetPath='%DEST%\screenBlank.exe'; $s.WorkingDirectory='%DEST%'; $s.Description='Make screens look switched off'; $s.Save()" || goto :fail

"%DEST%\screenBlank.exe" install-menu
start "" "%DEST%\screenBlank.exe"

echo Installed to %DEST%
echo Start Menu shortcut: screenBlank
echo Right-click the desktop or a folder background for the screenBlank menu
echo (Windows 11: under "Show more options").
echo screenBlank is running in the tray. Right-click it for options,
echo including "Start with Windows".
popd
exit /b 0

:fail
echo Install failed.
popd
exit /b 1
