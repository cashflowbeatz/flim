@echo off
REM Build flim.exe (launcher) + flim_engine.dll (injected engine).
REM Requires MSVC Build Tools. Output goes to client\out\.
setlocal

call "%~dp0msvc_env.bat"
if errorlevel 1 exit /b 1

set ROOT=%~dp0
set OUT=%ROOT%out
set FLIM_BUILD_FLAGS=
if "%FLIM_EXPERIMENTAL_FL26%"=="1" (
  set OUT=%ROOT%out-experimental
  set FLIM_BUILD_FLAGS=/DFLIM_EXPERIMENTAL_FL26
)
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"

echo === compiling engine resource ===
rc /nologo /fo "%OUT%\flim_engine.res" "%ROOT%engine\flim_engine.rc"
if errorlevel 1 exit /b 1

echo === compiling engine DLL ===
cl /nologo /O2 /W3 %FLIM_BUILD_FLAGS% /LD "%ROOT%engine\flim_engine.c" "%OUT%\flim_engine.res" /Fe:"%OUT%\flim_engine.dll" /Fo:"%OUT%\\" winhttp.lib
if errorlevel 1 exit /b 1

echo === compiling launcher resource ===
rc /nologo /fo "%OUT%\flim.res" "%ROOT%launcher\flim.rc"
if errorlevel 1 exit /b 1

echo === compiling launcher exe ===
cl /nologo /O2 /W3 %FLIM_BUILD_FLAGS% "%ROOT%launcher\flim.c" "%OUT%\flim.res" /Fe:"%OUT%\flim.exe" /Fo:"%OUT%\\" /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED user32.lib
if errorlevel 1 exit /b 1

if exist "%ROOT%profiles" (
  if not exist "%OUT%\profiles" mkdir "%OUT%\profiles"
  copy /Y "%ROOT%profiles\25.*.profile" "%OUT%\profiles\" >nul
  if "%FLIM_EXPERIMENTAL_FL26%"=="1" if exist "%ROOT%profiles\24.2.2.4597.profile" copy /Y "%ROOT%profiles\24.2.2.4597.profile" "%OUT%\profiles\" >nul
  if "%FLIM_EXPERIMENTAL_FL26%"=="1" copy /Y "%ROOT%profiles\26.*.profile" "%OUT%\profiles\" >nul
)

echo === done ===
dir /b "%OUT%"
popd
endlocal
