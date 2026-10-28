@echo off
setlocal
call "%~dp0..\msvc_env.bat"
if errorlevel 1 exit /b 1
set "FILES_ROOT=%~dp0.."
set "FILES_OUT=%FILES_ROOT%\out-file-sync"
if not exist "%FILES_OUT%" mkdir "%FILES_OUT%"
pushd "%FILES_OUT%"
rc /nologo /fo flim_engine.res "%FILES_ROOT%\engine\flim_engine.rc"
if errorlevel 1 goto failed
cl /nologo /O2 /W3 /DFLIM_EXPERIMENTAL_FL26 /LD "%FILES_ROOT%\engine\flim_engine.c" flim_engine.res /Fe:flim_engine.dll /Fo:.\ winhttp.lib
if errorlevel 1 goto failed
rc /nologo /fo flim.res "%FILES_ROOT%\launcher\flim.rc"
if errorlevel 1 goto failed
cl /nologo /O2 /W3 /DFLIM_EXPERIMENTAL_FL26 "%FILES_ROOT%\launcher\flim.c" flim.res /Fe:flim.exe /Fo:.\ /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED user32.lib
if errorlevel 1 goto failed
if not exist profiles mkdir profiles
if exist "%FILES_ROOT%\profiles\24.2.2.4597.profile" copy /Y "%FILES_ROOT%\profiles\24.2.2.4597.profile" profiles\ >nul
copy /Y "%FILES_ROOT%\profiles\25.*.profile" profiles\ >nul
if errorlevel 1 goto failed
copy /Y "%FILES_ROOT%\profiles\26.*.profile" profiles\ >nul
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
