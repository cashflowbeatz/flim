# Dev loop: close all FL instances, rebuild flim, relaunch leader "A" and member "B", both
# auto-connected (skips the Connect button). Server must be running: default ws://localhost:8080,
# or pass -Url / -Password (e.g. -Url wss://my-flim.fly.dev -Password ...).
param([string]$Url = "localhost:8080", [string]$Password = "")
Stop-Process -Name FL64 -Force -ErrorAction SilentlyContinue
Start-Sleep 2
$r = cmd /c "$PSScriptRoot\build.bat 2>&1"
if ($LASTEXITCODE -ne 0) { $r; exit 1 }
Remove-Item "$env:TEMP\flim_engine.log" -ErrorAction SilentlyContinue
$exe = "$PSScriptRoot\out\flim.exe"
$env:FLIM_AUTOCONNECT = $Url; $env:FLIM_PASSWORD = $Password
$env:FLIM_NAME = "A"; Start-Process $exe -WorkingDirectory (Split-Path $exe)
Start-Sleep 12
$env:FLIM_NAME = "B"; Start-Process $exe -WorkingDirectory (Split-Path $exe)
"build ok, launched A and B -> $Url"
