# Dev loop: close all FL instances, rebuild flim, relaunch leader "A" and member "B", both
# auto-connected (skips the Host / Connect buttons). A hosts, B joins session -Slot.
# Server must be running: default ws://localhost:8080, a self-hosted server (one session, 01;
# -OwnerKey = its FLIM_PASSWORD if it has one). Public server: e.g.
#   -Url flim.fly.dev -OwnerKey <owner key> -Slot 00    (or -OwnerKey "" -Slot 01 if 01 is free)
# -Password = the session password.
#   -Minimized   start both FLs minimized (they don't take over the screen)
#   -NoLocalB    B ignores local copies of samples (FLIM_SAMPLE_NOLOCAL): tests sample transfer on one PC
#   -FlDir <dir> start the FL in that folder instead of flim.ini's (e.g. another FL build)
#   -FlDirB <dir> B uses this FL instead (mixed-build session; default: same as A)
param([string]$Url = "localhost:8080", [string]$OwnerKey = "", [string]$Slot = "01", [string]$Password = "test",
      [switch]$Minimized, [switch]$NoLocalB, [string]$FlDir = "", [string]$FlDirB = "")
Stop-Process -Name FL64 -Force -ErrorAction SilentlyContinue
Start-Sleep 2
$r = cmd /c "$PSScriptRoot\build.bat 2>&1"
if ($LASTEXITCODE -ne 0) { $r; exit 1 }
Remove-Item "$env:TEMP\flim_engine.log" -ErrorAction SilentlyContinue
$exe = "$PSScriptRoot\out\flim.exe"
$style = if ($Minimized) { "Minimized" } else { "Normal" }
$sp = @{ FilePath = $exe; WorkingDirectory = (Split-Path $exe); WindowStyle = $style }
if ($FlDir) { $sp.ArgumentList = @("--fl-dir", "`"$FlDir`"") }
$env:FLIM_PASSWORD = $Password
$env:FLIM_AUTOCONNECT = $Url; $env:FLIM_HOST = "1"; $env:FLIM_OWNER_KEY = $OwnerKey
$env:FLIM_NAME = "A"; Start-Process @sp
Start-Sleep 12
if ($NoLocalB) { $env:FLIM_SAMPLE_NOLOCAL = "1" }
$env:FLIM_AUTOCONNECT = "$($Url.TrimEnd('/'))/$Slot"; $env:FLIM_HOST = ""; $env:FLIM_OWNER_KEY = ""
if ($FlDirB) { $sp.ArgumentList = @("--fl-dir", "`"$FlDirB`"") }
$env:FLIM_NAME = "B"; Start-Process @sp
"build ok, launched A (hosts) and B -> $Url/$Slot"
