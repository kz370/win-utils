@echo off
setlocal enabledelayedexpansion

rem Installs cpulimit for the current user and adds it to the user PATH.
rem No administrator rights required. Run uninstall.bat to undo this.

pushd "%~dp0"

set "INSTALL_DIR=%LOCALAPPDATA%\Programs\cpulimit"
if not "%~1"=="" set "INSTALL_DIR=%~1"

if not exist "cpulimit.exe" (
    echo cpulimit.exe not found. Building it first...
    call build.bat
    if errorlevel 1 (
        echo Build failed. Run build.bat manually and check the output.
        popd
        exit /b 1
    )
)

if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
copy /y "cpulimit.exe" "%INSTALL_DIR%\cpulimit.exe" >nul
if errorlevel 1 (
    echo Copy failed.
    popd
    exit /b 1
)
echo Installed to %INSTALL_DIR%

rem Read the current user PATH straight from the registry so we don't
rem clobber anything set outside this session.
set "CURRENT_PATH="
for /f "usebackq tokens=2,*" %%A in (`reg query "HKCU\Environment" /v Path 2^>nul`) do set "CURRENT_PATH=%%B"

echo !CURRENT_PATH!; | find /i "%INSTALL_DIR%;" >nul
if not errorlevel 1 (
    echo Already on the user PATH.
    goto :done
)

if defined CURRENT_PATH (
    set "NEW_PATH=!CURRENT_PATH!;%INSTALL_DIR%"
) else (
    set "NEW_PATH=%INSTALL_DIR%"
)

reg add "HKCU\Environment" /v Path /t REG_EXPAND_SZ /d "!NEW_PATH!" /f >nul
if errorlevel 1 (
    echo Failed to update PATH in the registry.
    popd
    exit /b 1
)
echo Added to the user PATH.

rem Broadcast the environment change so new processes pick it up without a
rem full logoff. Existing terminals still need to be reopened.
setx CPULIMIT_PATH_REFRESH "1" >nul 2>&1

:done
echo.
echo Done. Open a new terminal, then run:  cpulimit 20 ^<command^> [arguments...]
popd
exit /b 0
