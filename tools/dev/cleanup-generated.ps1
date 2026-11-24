#Requires -Version 7.0
[CmdletBinding()]
param(
    [switch]$DryRun,
    [switch]$Apply,
    [ValidateRange(1,1000)][int]$KeepLast = 3,
    [ValidateRange(0,36500)][double]$OlderThanDays = 1,
    [switch]$IncludeFailedRuns,
    [string[]]$LegacyBuildNames = @(),
    [string]$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')),
    [string]$OutputJson
)
$ErrorActionPreference = 'Stop'
if ($DryRun -and $Apply) { throw 'Choose -DryRun or -Apply, not both' }
. "$PSScriptRoot/generated-common.ps1"
$result = Invoke-AuraGeneratedCleanup -RepoRoot $RepoRoot -KeepLast $KeepLast -OlderThanDays $OlderThanDays -IncludeFailedRuns:$IncludeFailedRuns -Apply:$Apply -LegacyBuildNames $LegacyBuildNames
foreach ($decision in $result.decisions) {
    $size = $null -eq $decision.bytes ? 'Unknown' : "$($decision.bytes) bytes"
    Write-Host "$($decision.action): $($decision.path) | $size | $($decision.reason)"
}
$result.observed_free_delta_bytes = $result.free_bytes_after - $result.free_bytes_before
Write-Host "Estimated: $($result.estimated_reclaimable_bytes) bytes; deleted: $($result.deleted_logical_bytes) logical bytes; observed free-space delta: $($result.observed_free_delta_bytes) bytes."
if ($OutputJson) { $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputJson -Encoding utf8 }
if (@($result.decisions | Where-Object action -eq 'error').Count) { exit 1 }
