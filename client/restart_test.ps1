# Only exact identities recorded by an earlier run are stopped. Unrelated FL instances
# and the shared engine log are preserved. A hosts; B joins -Slot on the existing server.
param([string]$Url = 'localhost:8080', [string]$OwnerKey = '', [string]$Slot = '01',
      [string]$Password = 'test', [switch]$Minimized, [switch]$Visible, [switch]$Experimental,
      [switch]$NoLocalB, [string]$FlDir = '', [string]$FlDirB = '',
      [ValidateRange(5,120)][int]$StartupTimeoutSeconds = 30)

$ErrorActionPreference = 'Stop'
if ($Minimized -eq $Visible) { throw 'Choose exactly one of -Visible or -Minimized to launch the interactive FL test instances.' }
if ($Slot -notmatch '^\d{2}$') { throw '-Slot must be a two-digit session slot.' }
. (Join-Path $PSScriptRoot 'dev\test_process_ownership.ps1')
$repository = Get-FlimFullPath (Split-Path $PSScriptRoot)
$stateDirectory = Join-Path $PSScriptRoot 'out'
$manifestPath = Join-Path $stateDirectory 'restart-test-processes.json'

function Resolve-FlimTestDirectory {
    param([string]$Directory)
    if (-not $Directory) {
        $config = Join-Path $env:APPDATA 'flim\flim.ini'
        $inFlim = $false
        if (Test-Path -LiteralPath $config) {
            foreach ($line in Get-Content -LiteralPath $config) {
                if ($line -match '^\s*\[([^\]]+)\]') { $inFlim = $Matches[1] -ieq 'flim'; continue }
                if ($inFlim -and $line -match '^\s*fl_dir\s*=\s*(.*?)\s*$') { $Directory = $Matches[1]; break }
            }
        }
    }
    if (-not $Directory) { throw 'Specify -FlDir or configure fl_dir in flim.ini before running the test launcher.' }
    $resolved = Get-FlimFullPath $Directory
    if (-not (Test-Path -LiteralPath (Join-Path $resolved 'FL64.exe') -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $resolved 'FLEngine_x64.dll') -PathType Leaf)) {
        throw "FL64.exe and FLEngine_x64.dll are required in $resolved"
    }
    return $resolved
}

$directoryA = Resolve-FlimTestDirectory $FlDir
$directoryB = if ($FlDirB) { Resolve-FlimTestDirectory $FlDirB } else { $directoryA }
$prior = @(Read-FlimTestManifest $manifestPath $repository)
$null = New-Item -ItemType Directory -Path $stateDirectory -Force
$state = [pscustomobject]@{Records=[Collections.Generic.List[object]]::new()}
$runId = [Guid]::NewGuid().ToString('N')
$environmentNames = @('FLIM_EXPERIMENTAL_FL26','FLIM_PASSWORD','FLIM_AUTOCONNECT','FLIM_HOST',
                      'FLIM_OWNER_KEY','FLIM_NAME','FLIM_SAMPLE_NOLOCAL')
$savedEnvironment = @{}
foreach ($name in $environmentNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }

function Start-FlimOwnedInstance {
    param([string]$Role, [string]$Directory, [string]$LauncherExe)
    $started = [DateTime]::UtcNow
    $deadline = $started.AddSeconds($StartupTimeoutSeconds)
    $launcher = Start-Process -FilePath $LauncherExe -ArgumentList @('--fl-dir', "`"$Directory`"") `
        -WorkingDirectory (Split-Path $LauncherExe) -WindowStyle Hidden -PassThru
    try {
        $null = $launcher.Handle
        $launcherTicks = $launcher.StartTime.ToUniversalTime().Ticks.ToString()
        $launcherRecord = [pscustomobject]@{pid=$launcher.Id; start_time_utc_ticks=$launcherTicks
            exe=(Get-FlimFullPath $LauncherExe); kind='launcher'; role=$Role; run_id=$runId}
        $state.Records.Add($launcherRecord)
        Save-FlimTestManifest $manifestPath $repository $state.Records.ToArray()
        do {
            $children = @(Get-FlimOwnedChildren $launcher.Id $started.Ticks $deadline.Ticks (Join-Path $Directory 'FL64.exe'))
            if ($children.Count -gt 0) {
                foreach ($child in $children) {
                    $child | Add-Member -NotePropertyMembers @{kind='fl'; role=$Role; run_id=$runId
                        launcher_start_time_utc_ticks=$launcherTicks; launch_requested_utc=$started.ToString('o')}
                    $state.Records.Add($child)
                }
                Save-FlimTestManifest $manifestPath $repository $state.Records.ToArray()
                if ($children.Count -ne 1) { throw "Launcher $($launcher.Id) created multiple matching FL children; identities saved, startup stopped." }
                if ($Minimized) { Set-FlimOwnedWindowMinimized $children[0] $StartupTimeoutSeconds }
                return $children[0]
            }
            if ($launcher.HasExited -and $launcher.ExitCode -ne 0) { throw "Launcher $($launcher.Id) exited with code $($launcher.ExitCode); no owned FL child established." }
            Start-Sleep -Milliseconds 250
        } while ([DateTime]::UtcNow -lt $deadline)
        throw "Could not establish a unique FL child of launcher $($launcher.Id) before timeout; no unrelated processes were stopped."
    } finally { $launcher.Dispose() }
}

try {
    Stop-FlimOwnedProcesses $prior
    Save-FlimTestManifest $manifestPath $repository @()
    $env:FLIM_EXPERIMENTAL_FL26 = if ($Experimental) { '1' } else { $null }
    $buildStdout = Join-Path $stateDirectory "restart-build-$runId.stdout.log"
    $buildStderr = Join-Path $stateDirectory "restart-build-$runId.stderr.log"
    $build = Start-Process -FilePath $env:ComSpec -ArgumentList @('/d', '/c', "`"$PSScriptRoot\build.bat`"") `
        -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput $buildStdout -RedirectStandardError $buildStderr
    try { if ($build.ExitCode -ne 0) { throw "Build failed with code $($build.ExitCode). See $buildStdout and $buildStderr" } }
    finally { $build.Dispose() }
    $outputDirectory = if ($Experimental) { 'out-experimental' } else { 'out' }
    $exe = Join-Path $PSScriptRoot "$outputDirectory\flim.exe"
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Build did not produce $exe" }
    $env:FLIM_PASSWORD = $Password
    $env:FLIM_SAMPLE_NOLOCAL = $null
    $env:FLIM_AUTOCONNECT = $Url; $env:FLIM_HOST = '1'; $env:FLIM_OWNER_KEY = $OwnerKey
    $env:FLIM_NAME = 'A'
    $leader = Start-FlimOwnedInstance 'A' $directoryA $exe
    Start-Sleep -Seconds 12
    $env:FLIM_SAMPLE_NOLOCAL = if ($NoLocalB) { '1' } else { $null }
    $env:FLIM_AUTOCONNECT = "$($Url.TrimEnd('/'))/$Slot"; $env:FLIM_HOST = $null; $env:FLIM_OWNER_KEY = $null
    $env:FLIM_NAME = 'B'
    $member = Start-FlimOwnedInstance 'B' $directoryB $exe
    Write-Output "Build complete; owned FL PIDs A=$($leader.pid), B=$($member.pid). Manifest: $manifestPath"
} finally {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}
