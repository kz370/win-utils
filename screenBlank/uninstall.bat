@echo off
setlocal

set "DEST=%~1"
if "%DEST%"=="" set "DEST=%LOCALAPPDATA%\Programs\screenBlank"

if exist "%DEST%\screenBlank.exe" "%DEST%\screenBlank.exe" exit >nul 2>&1
timeout /t 1 /nobreak >nul
taskkill /im screenBlank.exe /f >nul 2>&1

reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v screenBlank /f >nul 2>&1
reg delete "HKCU\Software\Classes\Directory\Background\shell\screenBlank" /f >nul 2>&1
del /q "%APPDATA%\Microsoft\Windows\Start Menu\Programs\screenBlank.lnk" 2>nul
if exist "%DEST%" rmdir /s /q "%DEST%"

echo screenBlank removed.
echo Settings kept in %APPDATA%\screenBlank (delete that folder to remove them too).
exit /b 0
