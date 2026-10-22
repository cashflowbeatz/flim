@echo off
setlocal
cd /d "%~dp0"
call "..\..\msvc_env.bat"
if errorlevel 1 exit /b 1
cl /nologo /W3 t_samples.c /Fe:t_samples.exe winhttp.lib shell32.lib user32.lib gdi32.lib bcrypt.lib advapi32.lib
if errorlevel 1 exit /b 1
cl /nologo /W4 t_ws_buffer.c /Fe:t_ws_buffer.exe
if errorlevel 1 exit /b 1
cl /nologo /W3 t_launcher.c /Fe:t_launcher.exe /link /SUBSYSTEM:CONSOLE /MANIFEST:EMBED
if errorlevel 1 exit /b 1
cl /nologo /W3 t_injection.c /Fe:t_injection.exe /link /SUBSYSTEM:CONSOLE /MANIFEST:EMBED
if errorlevel 1 exit /b 1
cl /nologo /W3 t_patops.c /Fe:t_patops.exe
if errorlevel 1 exit /b 1
cl /nologo /W4 t_flp.c /Fe:t_flp.exe
if errorlevel 1 exit /b 1
cl /nologo /W3 t_plugin_apply.c /Fe:t_plugin_apply.exe winhttp.lib shell32.lib user32.lib gdi32.lib bcrypt.lib advapi32.lib
if errorlevel 1 exit /b 1
