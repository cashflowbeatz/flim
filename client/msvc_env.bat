@echo off
REM Use an x64 developer prompt, an explicit VCVARS, or the installed C++ toolchain.
if /i "%VSCMD_ARG_TGT_ARCH%"=="x64" exit /b 0
if defined VCVARS goto configured
set "FLIM_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%FLIM_VSWHERE%" goto missing
for /f "usebackq tokens=*" %%i in (`"%FLIM_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
:configured
if not defined VCVARS goto missing
if not exist "%VCVARS%" goto missing
call "%VCVARS%" >nul
exit /b %errorlevel%
:missing
echo No x64 MSVC C++ toolchain found. Install Visual Studio C++ Build Tools or set VCVARS to vcvars64.bat.
exit /b 1
