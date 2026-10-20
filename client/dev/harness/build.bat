@echo off
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W3 t_samples.c /Fe:t_samples.exe winhttp.lib shell32.lib user32.lib gdi32.lib bcrypt.lib advapi32.lib
