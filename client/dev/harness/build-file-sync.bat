@echo off
setlocal
call "%~dp0..\..\msvc_env.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0"
cl /nologo /W3 t_file_sync.c /Fe:t_file_sync.exe winhttp.lib shell32.lib user32.lib gdi32.lib bcrypt.lib advapi32.lib
set FILE_SYNC_RESULT=%errorlevel%
popd
exit /b %FILE_SYNC_RESULT%
