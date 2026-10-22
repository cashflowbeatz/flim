@echo off
setlocal
cd /d "%~dp0"
call "..\..\msvc_env.bat"
if errorlevel 1 exit /b 1
cl /nologo /O1 /LD probe.c /Fe:probe.dll user32.lib
