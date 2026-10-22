# Pure identity fixtures plus mocked process operations. No real processes are launched or stopped.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'test_process_ownership.ps1')
$checks = 0
function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    $script:checks++
}
function New-Identity([int]$Number, [string]$Ticks='638000000000000000', [string]$Exe='C:\Fixture\FL64.exe') {
    [pscustomobject]@{pid=$Number; start_time_utc_ticks=$Ticks; exe=$Exe; kind='fl'}
}
$owned = New-Identity 100
Assert-True (Test-FlimProcessIdentity $owned (New-Identity 100)) 'identical process is owned'
Assert-True (-not (Test-FlimProcessIdentity $owned (New-Identity 101))) 'different PID is not owned'
Assert-True (-not (Test-FlimProcessIdentity $owned (New-Identity 100 '638000000000000001'))) 'reused PID is not owned'
Assert-True (-not (Test-FlimProcessIdentity $owned (New-Identity 100 '638000000000000000' 'C:\Other\FL64.exe'))) 'different executable is not owned'
Assert-True (Test-FlimProcessIdentity $owned (New-Identity 100 '638000000000000000' 'c:\fixture\fl64.exe')) 'Windows paths compare case-insensitively'
$child = New-Identity 200 '150'
$child | Add-Member parent_pid 88
Assert-True (Test-FlimChildCandidate $child 88 100 200 'C:\Fixture\FL64.exe') 'matching newly created child is selected'
Assert-True (-not (Test-FlimChildCandidate $child 89 100 200 'C:\Fixture\FL64.exe')) 'wrong parent is rejected'
Assert-True (-not (Test-FlimChildCandidate $child 88 151 200 'C:\Fixture\FL64.exe')) 'preexisting child is rejected'
Assert-True (-not (Test-FlimChildCandidate $child 88 100 149 'C:\Fixture\FL64.exe')) 'late child is rejected'
Assert-True (-not (Test-FlimChildCandidate $child 88 100 200 'C:\Other\FL64.exe')) 'wrong child executable is rejected'

$repository = Get-FlimFullPath (Join-Path $PSScriptRoot '..\..')
$scratch = Get-FlimFullPath (Join-Path $repository ('..\ownership-tests\' + [Guid]::NewGuid().ToString('N')))
$null = New-Item -ItemType Directory -Path $scratch -Force
$manifest = Join-Path $scratch 'fixture.json'
Assert-True (@(Read-FlimTestManifest $manifest $repository).Count -eq 0) 'absent manifest owns nothing'
Save-FlimTestManifest $manifest $repository @($owned)
$loaded = @(Read-FlimTestManifest $manifest $repository)
Assert-True ($loaded.Count -eq 1 -and (Test-FlimProcessIdentity $loaded[0] $owned)) 'manifest roundtrip preserves precise identity'
Save-FlimTestManifest $manifest $repository @()
Assert-True (@(Read-FlimTestManifest $manifest $repository).Count -eq 0) 'empty manifest owns nothing'
Save-FlimTestManifest $manifest $repository @($owned, $owned)
$rejected = $false
try { Read-FlimTestManifest $manifest $repository | Out-Null } catch { $rejected = $true }
Assert-True $rejected 'duplicate manifest PIDs fail closed'
Save-FlimTestManifest $manifest $repository @($owned)
$rejected = $false
try { Read-FlimTestManifest $manifest 'C:\DifferentRepository' | Out-Null } catch { $rejected = $true }
Assert-True $rejected 'different manifest repository fails closed'

$script:fixtureProcesses = @{}
$script:stopped = [Collections.Generic.List[int]]::new()
foreach ($identity in @($owned, (New-Identity 101 '638000000000000001'),
                         (New-Identity 102 '638000000000000000' 'C:\Other\FL64.exe'), (New-Identity 106748))) {
    $fake = [pscustomobject]@{Identity=$identity}
    $fake | Add-Member ScriptMethod Dispose {}
    $fake | Add-Member ScriptMethod WaitForExit { param($Timeout) return $true }
    $script:fixtureProcesses[[int]$identity.pid] = $fake
}
function Get-Process {
    [CmdletBinding()]param([int]$Id)
    return $script:fixtureProcesses[$Id]
}
function Get-FlimProcessIdentity { param($Process) return $Process.Identity }
function Stop-Process {
    [CmdletBinding()]param($InputObject, [switch]$Force)
    $script:stopped.Add([int]$InputObject.Identity.pid)
}
Stop-FlimOwnedProcesses @($owned, (New-Identity 101), (New-Identity 102), (New-Identity 103)) -WarningAction SilentlyContinue
Assert-True ($script:stopped.Count -eq 1 -and $script:stopped[0] -eq 100) 'only exact manifest identity is stopped; unrelated original PID is preserved'

$scriptPath = Join-Path $PSScriptRoot '..\restart_test.ps1'
$tokens = $null; $parseErrors = $null
$null = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors)
Assert-True ($parseErrors.Count -eq 0) 'restart script parses without execution'
$source = Get-Content -LiteralPath $scriptPath -Raw
Assert-True ($source -notmatch 'Stop-Process\s+-Name') 'script never stops by process name'
Assert-True ($source -notmatch 'Remove-Item') 'script preserves shared log and existing files'
Assert-True ($source -match 'FLIM_EXPERIMENTAL_FL26' -and $source -match 'out-experimental') 'experimental build and output selected together'
Write-Output "PASS: $checks ownership, manifest, mock-stop, and syntax checks; no real process actions."
