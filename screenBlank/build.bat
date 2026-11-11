@echo off
setlocal enabledelayedexpansion

pushd "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" (
    echo vswhere.exe not found. Install Visual Studio or the Build Tools.
    goto :fail
)

set "VSPATH="
for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if not defined VSPATH (
    echo Visual Studio with the C++ toolset was not found.
    goto :fail
)

call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || goto :fail

if not exist screenBlank.ico (
    powershell -NoProfile -ExecutionPolicy Bypass -File make-icon.ps1 || goto :fail
)

rc /nologo /fo screenBlank.res screenBlank.rc || goto :fail
cl /nologo /EHsc /W4 /O2 /std:c++17 /DUNICODE /D_UNICODE screenBlank.cpp screenBlank.res /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /out:screenBlank.exe || goto :fail

del /q screenBlank.res screenBlank.obj 2>nul
echo Built screenBlank.exe
popd
exit /b 0

:fail
echo Build failed.
popd
exit /b 1
