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

rc /nologo /fo cpulimit.res cpulimit.rc || goto :fail
cl /nologo /EHsc /W4 /O2 /std:c++17 cpulimit.cpp cpulimit.res /link /out:cpulimit.exe || goto :fail

del /q cpulimit.res cpulimit.obj 2>nul
echo Built cpulimit.exe
popd
exit /b 0

:fail
echo Build failed.
popd
exit /b 1
