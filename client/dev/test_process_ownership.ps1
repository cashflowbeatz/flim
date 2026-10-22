# Identity helpers for restart_test.ps1. Dot-sourcing this file performs no lifecycle actions.
function Get-FlimFullPath {
    param([Parameter(Mandatory)][string]$Path)
    [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}

function Test-FlimProcessIdentity {
    param([Parameter(Mandatory)]$Expected, [Parameter(Mandatory)]$Observed)
    return ([long]$Expected.pid -eq [long]$Observed.pid -and
            [string]$Expected.start_time_utc_ticks -ceq [string]$Observed.start_time_utc_ticks -and
            [string]::Equals((Get-FlimFullPath $Expected.exe), (Get-FlimFullPath $Observed.exe),
                            [StringComparison]::OrdinalIgnoreCase))
}

function Test-FlimChildCandidate {
    param([Parameter(Mandatory)]$Child, [int]$LauncherPid, [long]$StartedTicks,
          [long]$DeadlineTicks, [Parameter(Mandatory)][string]$ExpectedExe)
    return ([long]$Child.parent_pid -eq $LauncherPid -and
            [long]$Child.start_time_utc_ticks -ge $StartedTicks -and
            [long]$Child.start_time_utc_ticks -le $DeadlineTicks -and
            [string]::Equals((Get-FlimFullPath $Child.exe), (Get-FlimFullPath $ExpectedExe),
                            [StringComparison]::OrdinalIgnoreCase))
}

function Get-FlimProcessIdentity {
    param([Parameter(Mandatory)][System.Diagnostics.Process]$Process)
    # Pin a handle before inspecting identity or using Stop-Process -InputObject.
    $null = $Process.Handle
    [pscustomobject]@{pid=$Process.Id; start_time_utc_ticks=$Process.StartTime.ToUniversalTime().Ticks.ToString()
                     exe=(Get-FlimFullPath $Process.MainModule.FileName)}
}

function Read-FlimTestManifest {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Repository)
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($manifest.schema_version -ne 1 -or
        -not [string]::Equals((Get-FlimFullPath $manifest.repository), (Get-FlimFullPath $Repository),
                             [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Test-process manifest has an unexpected schema or repository; no processes were stopped.'
    }
    $seen = @{}
    foreach ($record in @($manifest.records)) {
        $ticks = 0L
        if ($null -eq $record -or [long]$record.pid -le 0 -or
            -not [long]::TryParse([string]$record.start_time_utc_ticks, [ref]$ticks) -or $ticks -le 0 -or
            -not [System.IO.Path]::IsPathRooted([string]$record.exe) -or
            $record.kind -notin @('launcher', 'fl') -or $seen.ContainsKey([string]$record.pid)) {
            throw 'Test-process manifest contains invalid or duplicate identities; no processes were stopped.'
        }
        $expectedName = if ($record.kind -eq 'fl') { 'FL64.exe' } else { 'flim.exe' }
        if ([System.IO.Path]::GetFileName($record.exe) -ine $expectedName) {
            throw 'Test-process manifest executable kind is invalid; no processes were stopped.'
        }
        if ($record.kind -eq 'launcher') {
            $allowed = @((Join-Path $Repository 'client\out\flim.exe'), (Join-Path $Repository 'client\out-experimental\flim.exe'))
            if (-not ($allowed | Where-Object { [string]::Equals((Get-FlimFullPath $_),
                     (Get-FlimFullPath $record.exe), [StringComparison]::OrdinalIgnoreCase) })) {
                throw 'Test-process manifest launcher is outside this checkout; no processes were stopped.'
            }
        }
        $seen[[string]$record.pid] = $true
    }
    return @($manifest.records)
}

function Save-FlimTestManifest {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Repository,
          [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Records)
    $document = [ordered]@{schema_version=1; repository=(Get-FlimFullPath $Repository)
                         updated_utc=[DateTime]::UtcNow.ToString('o'); records=@($Records)}
    $temporary = "$Path.tmp"
    [IO.File]::WriteAllText($temporary, ($document | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Stop-FlimOwnedProcesses {
    param([Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Records)
    foreach ($record in $Records) {
        $process = Get-Process -Id ([int]$record.pid) -ErrorAction SilentlyContinue
        if ($null -eq $process) { continue }
        try {
            $identity = Get-FlimProcessIdentity $process
            if (-not (Test-FlimProcessIdentity $record $identity)) {
                Write-Warning "Preserving PID $($record.pid): creation time or executable no longer matches the test manifest."
                continue
            }
            Stop-Process -InputObject $process -Force -ErrorAction Stop
            if (-not $process.WaitForExit(10000)) { throw "Owned PID $($record.pid) did not exit." }
        } finally { $process.Dispose() }
    }
}

function Get-FlimOwnedChildren {
    param([int]$LauncherPid, [long]$StartedTicks, [long]$DeadlineTicks,
          [Parameter(Mandatory)][string]$ExpectedExe)
    $snapshot = @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$LauncherPid AND Name='FL64.exe'")
    foreach ($child in $snapshot) {
        $process = Get-Process -Id ([int]$child.ProcessId) -ErrorAction SilentlyContinue
        if ($null -eq $process) { continue }
        try {
            $identity = Get-FlimProcessIdentity $process
            $cimTicks = ([DateTime]$child.CreationDate).ToUniversalTime().Ticks
            # CIM truncates to microseconds; reject a PID reused after its snapshot.
            if ([Math]::Abs([long]$identity.start_time_utc_ticks - $cimTicks) -gt 10) { continue }
            $identity | Add-Member -NotePropertyName parent_pid -NotePropertyValue ([int]$child.ParentProcessId)
            if (Test-FlimChildCandidate $identity $LauncherPid $StartedTicks $DeadlineTicks $ExpectedExe) { $identity }
        } finally { $process.Dispose() }
    }
}

function Set-FlimOwnedWindowMinimized {
    param([Parameter(Mandatory)]$Identity, [int]$TimeoutSeconds=30)
    if (-not ('FlimTestWindow' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FlimTestWindow {
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr hwnd, int command);
}
'@
    }
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $process = Get-Process -Id ([int]$Identity.pid) -ErrorAction Stop
        try {
            if (-not (Test-FlimProcessIdentity $Identity (Get-FlimProcessIdentity $process))) {
                throw 'Test process identity changed before minimizing; no window action taken.'
            }
            $window = $process.MainWindowHandle
            if ($window -ne [IntPtr]::Zero) {
                $windowPid = [uint32]0
                $null = [FlimTestWindow]::GetWindowThreadProcessId($window, [ref]$windowPid)
                if ($windowPid -ne [uint32]$Identity.pid) { throw 'Test window PID does not match its owned process.' }
                if (-not [FlimTestWindow]::ShowWindowAsync($window, 6)) { throw 'Could not minimize the owned test window.' }
                return
            }
        } finally { $process.Dispose() }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Owned FL test process did not expose a main window to minimize before timeout.'
}
