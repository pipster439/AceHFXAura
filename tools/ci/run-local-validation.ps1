#Requires -Version 7.0
[CmdletBinding()]
param(
    [switch]$HardwareSmoke, [switch]$AllowHardwareWrites,
    [string]$ProfileA, [string]$ProfileB,
    [string]$OutputDirectory = 'artifacts/ci',
    [string]$DaemonUrl = 'http://127.0.0.1:19897'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/ci-common.ps1"
if ($AllowHardwareWrites -and (-not $HardwareSmoke -or (Test-CiEnvironment))) {
    throw 'Hardware writes require local HardwareSmoke and are forbidden in CI.'
}
# Separate processes retain the software summary even if the optional smoke fails.
& pwsh -NoProfile -File "$PSScriptRoot/run-ci.ps1" -OutputDirectory $OutputDirectory
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($HardwareSmoke) {
    if (-not (Test-CiEnvironment)) {
        Write-Host 'Software CI completed. Start your reviewed daemon normally before the optional local smoke.'
        Read-Host 'Daemon ready: press Enter to continue' | Out-Null
    }
    & pwsh -NoProfile -File "$PSScriptRoot/../hardware/run-profile-automation-smoke.ps1" `
        -ProfileA $ProfileA -ProfileB $ProfileB -DaemonUrl $DaemonUrl -AllowHardwareWrites:$AllowHardwareWrites
    exit $LASTEXITCODE
}
