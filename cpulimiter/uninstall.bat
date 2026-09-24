@echo off
setlocal enabledelayedexpansion

rem Removes cpulimit and takes its directory back off the user PATH.

set "INSTALL_DIR=%LOCALAPPDATA%\Programs\cpulimit"
if not "%~1"=="" set "INSTALL_DIR=%~1"

set "CURRENT_PATH="
for /f "usebackq tokens=2,*" %%A in (`reg query "HKCU\Environment" /v Path 2^>nul`) do set "CURRENT_PATH=%%B"

if not defined CURRENT_PATH (
    echo Not on the user PATH.
    goto :removefiles
)

set "NEW_PATH="
for %%P in ("!CURRENT_PATH:;=";"!") do (
    set "ENTRY=%%~P"
    if /i not "!ENTRY!"=="%INSTALL_DIR%" if not "!ENTRY!"=="" (
        if defined NEW_PATH (
            set "NEW_PATH=!NEW_PATH!;!ENTRY!"
        ) else (
            set "NEW_PATH=!ENTRY!"
        )
    )
)

if "!NEW_PATH!"=="!CURRENT_PATH!" (
    echo Not on the user PATH.
) else (
    reg add "HKCU\Environment" /v Path /t REG_EXPAND_SZ /d "!NEW_PATH!" /f >nul
    echo Removed from the user PATH.
)

:removefiles
if exist "%INSTALL_DIR%" (
    rmdir /s /q "%INSTALL_DIR%"
    echo Deleted %INSTALL_DIR%
) else (
    echo %INSTALL_DIR% does not exist.
)

echo Done. Open a new terminal for the PATH change to take effect.
exit /b 0
