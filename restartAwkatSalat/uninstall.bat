@echo off
setlocal

set "TASK_NAME=Restart Awqat on Explorer Restart"
set "INSTALL_DIR=%ProgramData%\AwqatRestart"

rem Self-elevate: relaunch this script with a UAC prompt if not admin
fltmc >nul 2>&1 && goto ELEVATED
powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
exit /b

:ELEVATED
schtasks /delete /tn "%TASK_NAME%" /f
rmdir /s /q "%INSTALL_DIR%" 2>nul
del "%LOCALAPPDATA%\awqat-explorer-pid.txt" >nul 2>&1

echo.
echo Uninstalled.
pause
exit /b 0