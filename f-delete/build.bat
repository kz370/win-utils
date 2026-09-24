@echo off
REM Build fdel.exe locally with the .NET Framework compiler bundled in Windows.
setlocal
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" (
  echo Could not find csc.exe at "%CSC%".
  exit /b 1
)
"%CSC%" -nologo -optimize+ -platform:x64 -win32icon:fdel.ico -win32manifest:fdel.manifest -out:fdel.exe fdel.cs
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)
echo Built fdel.exe
