#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateRange(1,5)][int]$SlotA = 5,
    [ValidateRange(1,5)][int]$SlotB = 1,
    [string]$ProfileA,
    [string]$ProfileB,
    [switch]$AllowHardwareWrites,
    [switch]$ManualHold,
    [string]$DaemonUrl = 'http://127.0.0.1:19897',
    [string]$OutputDirectory,
    [ValidateRange(5,600)][int]$StageTimeoutSeconds = 120
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/smoke-common.ps1"
# Fail closed before even querying HTTP or creating evidence.
Assert-LocalSmokePermission -AllowHardwareWrites:$AllowHardwareWrites -DaemonUrl $DaemonUrl
$smokeArgs = @{ DaemonUrl=$DaemonUrl; StageTimeoutSeconds=$StageTimeoutSeconds }
if ($ManualHold) { $smokeArgs.ManualHold=$true }
if ($OutputDirectory) { $smokeArgs.OutputDirectory=$OutputDirectory }
if ($AllowHardwareWrites) {
    if ($SlotA -eq $SlotB) { throw 'Choose two distinct existing onboard slots.' }
    $response = Invoke-RestMethod -Uri ($DaemonUrl.TrimEnd('/') + '/api/device-profiles') -TimeoutSec 10
    function Resolve-SlotProfile([int]$Slot,[string]$Requested) {
        $matches = @($response.profiles | Where-Object {
            ($_.PSObject.Properties.Name -contains 'activation_backend') -and $_.activation_backend -eq 'hardware_slot' -and $_.hardware_slot -eq $Slot -and
            (-not $Requested -or $_.id -eq $Requested -or $_.name -eq $Requested)
        })
        if ($matches.Count -ne 1) { throw "Slot $Slot requires one existing Aura hardware-slot Profile; specify -ProfileA/-ProfileB if ambiguous. No Profile/bank is created." }
        return [string]$matches[0].id
    }
    $smokeArgs.ProfileA=Resolve-SlotProfile $SlotA $ProfileA
    $smokeArgs.ProfileB=Resolve-SlotProfile $SlotB $ProfileB
    $smokeArgs.AllowHardwareWrites=$true
    $smokeArgs.ExpectedHardwareSlotA=$SlotA; $smokeArgs.ExpectedHardwareSlotB=$SlotB
}
# Reuse revisioned temporary rules, real foreground confirmation, duplicate
# suppression, quarantine checks, sanitized evidence and finally restoration.
& "$PSScriptRoot/run-profile-automation-smoke.ps1" @smokeArgs -ProgramA notepad.exe -ProgramB charmap.exe
