::[Bat To Exe Converter]
::
::YAwzoRdxOk+EWAjk
::fBw5plQjdCiDJH2L40w8JxpQXziDK2C7EoYd5OnvoeOErS0=
::YAwzuBVtJxjWCl3EqQJgSA==
::ZR4luwNxJguZRRnk
::Yhs/ulQjdF+5
::cxAkpRVqdFKZSzk=
::cBs/ulQjdF+5
::ZR41oxFsdFKZSDk=
::eBoioBt6dFKZSDk=
::cRo6pxp7LAbNWATEpCI=
::egkzugNsPRvcWATEpCI=
::dAsiuh18IRvcCxnZtBJQ
::cRYluBh/LU+EWAnk
::YxY4rhs+aU+JeA==
::cxY6rQJ7JhzQF1fEqQJQ
::ZQ05rAF9IBncCkqN+0xwdVs0
::ZQ05rAF9IAHYFVzEqQJQ
::eg0/rx1wNQPfEVWB+kM9LVsJDGQ=
::fBEirQZwNQPfEVWB+kM9LVsJDGQ=
::cRolqwZ3JBvQF1fEqQJQ
::dhA7uBVwLU+EWDk=
::YQ03rBFzNR3SWATElA==
::dhAmsQZ3MwfNWATElA==
::ZQ0/vhVqMQ3MEVWAtB9wSA==
::Zg8zqx1/OA3MEVWAtB9wSA==
::dhA7pRFwIByZRRnk
::Zh4grVQjdCiDJH2L40w8JxpQXziwOXiuB6cIyf/q7v7Jp1UYNA==
::YB416Ek+ZG8=
::
::
::978f952a14a936cc963da21a135fa983
@echo off
setlocal EnableDelayedExpansion
set "STATE=%LOCALAPPDATA%\awqat-explorer-pid.txt"

:WAIT_TASKBAR
powershell -NoProfile -Command "Add-Type 'using System;using System.Runtime.InteropServices;public class W{[DllImport(\"user32.dll\")]public static extern IntPtr FindWindow(string c,string t);}'; if([W]::FindWindow('Shell_TrayWnd',$null) -eq [IntPtr]::Zero){exit 1}"
if errorlevel 1 (timeout /t 1 /nobreak >nul & goto WAIT_TASKBAR)

rem Taskbar exists: give the tray a moment to finish loading, then re-check
timeout /t 3 /nobreak >nul
powershell -NoProfile -Command "Add-Type 'using System;using System.Runtime.InteropServices;public class W{[DllImport(\"user32.dll\")]public static extern IntPtr FindWindow(string c,string t);}'; if([W]::FindWindow('Shell_TrayWnd',$null) -eq [IntPtr]::Zero){exit 1}"
if errorlevel 1 goto WAIT_TASKBAR

call :GETPIDS

rem Skip if this explorer instance was already handled
if exist "%STATE%" (
    set /p LAST=<"%STATE%"
    if "!LAST!"=="!PIDS!" exit /b
)
>"%STATE%" echo !PIDS!

rem Restart the app
taskkill /f /im "AwqatSalaat.WinUI.exe" >nul 2>&1
timeout /t 1 /nobreak >nul
setlocal DisableDelayedExpansion
start "" "shell:AppsFolder\Khiro.AwqatSalaatWinUI_343nwpyy1ep3t!App"
endlocal
exit /b

:GETPIDS
set "PIDS="
for /f "tokens=2 delims=," %%a in ('tasklist /FI "IMAGENAME eq explorer.exe" /FO CSV /NH 2^>nul') do set "PIDS=!PIDS!%%~a-"
exit /b