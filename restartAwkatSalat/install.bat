@echo off
setlocal

set "TASK_NAME=Restart Awqat on Explorer Restart"
set "INSTALL_DIR=%ProgramData%\AwqatRestart"
set "EXE_NAME=RestartAwqat.exe"
set "XML_SRC=%~dp0RestartAwqat.xml"
set "XML_TMP=%TEMP%\RestartAwqat.install.xml"

rem Self-elevate: relaunch this script with a UAC prompt if not admin
fltmc >nul 2>&1 && goto ELEVATED
powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
exit /b

:ELEVATED
if not exist "%XML_SRC%" goto MISSING_XML
if not exist "%~dp0%EXE_NAME%" goto MISSING_EXE

rem 1. Copy the exe to a fixed location
mkdir "%INSTALL_DIR%" 2>nul
copy /y "%~dp0%EXE_NAME%" "%INSTALL_DIR%\%EXE_NAME%" >nul
if errorlevel 1 goto FAIL

rem 2. Enable process-creation auditing (needed for event 4688)
auditpol /set /subcategory:"{0CCE922B-69AE-11D9-BED3-505054503030}" /success:enable >nul
if errorlevel 1 goto FAIL

rem 3. Fill in __EXE_PATH__ and save as UTF-16
powershell -NoProfile -Command "$t=[IO.File]::ReadAllText($env:XML_SRC); $t=$t.Replace('__EXE_PATH__',[Security.SecurityElement]::Escape($env:INSTALL_DIR+'\'+$env:EXE_NAME)); [IO.File]::WriteAllText($env:XML_TMP,$t,[Text.Encoding]::Unicode)"
if errorlevel 1 goto FAIL

rem 4. Register the task
schtasks /create /tn "%TASK_NAME%" /xml "%XML_TMP%" /f
if errorlevel 1 goto FAIL
del "%XML_TMP%" >nul 2>&1

echo.
echo Installed. Task: "%TASK_NAME%"
echo Program: %INSTALL_DIR%\%EXE_NAME%
pause
exit /b 0

:MISSING_XML
echo Missing file: %XML_SRC%
pause
exit /b 1

:MISSING_EXE
echo Missing file: %~dp0%EXE_NAME%
pause
exit /b 1

:FAIL
echo.
echo Something failed. See the message above.
pause
exit /b 1