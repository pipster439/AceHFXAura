#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$ProfileA, [string]$ProfileB,
    [switch]$AllowHardwareWrites,
    [switch]$ManualHold,
    [string]$DaemonUrl = 'http://127.0.0.1:19897',
    [string]$OutputDirectory,
    [ValidateRange(5, 600)][int]$StageTimeoutSeconds = 120
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/smoke-common.ps1"
# Reject before any HTTP request, file creation, app launch or configuration mutation.
Assert-LocalSmokePermission -AllowHardwareWrites:$AllowHardwareWrites -DaemonUrl $DaemonUrl
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $OutputDirectory) { $OutputDirectory = 'artifacts/hardware-smoke-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') }
$output = [IO.Path]::GetFullPath($OutputDirectory, $repo)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$summary = [ordered]@{
    schema_version = 1; mode = $(if ($AllowHardwareWrites) { 'LocalHardwareWrites' } else { 'ReadOnly' })
    overall = 'running'; hardware_behavior = 'NOT VERIFIED; host observation only'
    started_at_utc = [DateTime]::UtcNow.ToString('o'); completed_at_utc = $null
    stages = @(); temporary_rule_ids = @(); original_automation_enabled = $null
    restoration = 'not-required'; error = $null
}
$restoreRequired = $false; $original = $null; $installed = $null; $instance = $null
function Invoke-SmokeApi {
    param([string]$Path, $Body = $null)
    $args = @{ Uri = $DaemonUrl.TrimEnd('/') + '/api/device-profiles' + $Path; TimeoutSec = 180 }
    if ($null -ne $Body) {
        if (-not $AllowHardwareWrites) { throw 'Mutation forbidden in read-only mode.' }
        $args.Method = 'Post'; $args.ContentType = 'application/json'
        $args.Body = $Body | ConvertTo-Json -Depth 100 -Compress
    }
    return (Invoke-RestMethod @args | ConvertTo-Json -Depth 100 | ConvertFrom-Json -AsHashtable)
}
function Save-SmokeSnapshot {
    param([string]$Name, [string]$Foreground = $null)
    $response = Invoke-SmokeApi '/diagnostics'
    if ($response.api_version -ne 1 -or $response.diagnostics.diagnostic_schema_version -ne 1) { throw 'Unsupported diagnostics contract.' }
    $evidence = Get-SmokeEvidence $response.diagnostics $Foreground
    $evidence | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $output "$Name.json") -Encoding utf8
    return $evidence
}
function Wait-SmokeTarget {
    param([string]$Name, [string]$Process, [string]$Target)
    $start = Save-SmokeSnapshot "$Name-start" ([AuraSmokeForeground]::Basename())
    # Users focus the real application. SetForegroundWindow is deliberately not faked or forced.
    Write-Host "请打开并切换到 $Process，保持前台直到本阶段完成。" -ForegroundColor Cyan
    Start-Process -FilePath $Process -WindowStyle Normal | Out-Null
    $timer = [Diagnostics.Stopwatch]::StartNew(); $stable = [Diagnostics.Stopwatch]::new()
    while ($timer.Elapsed.TotalSeconds -lt $StageTimeoutSeconds) {
        $foreground = [AuraSmokeForeground]::Basename()
        if ($foreground -eq $Process) {
            if (-not $stable.IsRunning) { $stable.Restart() }
        } else { $stable.Reset() }
        $evidence = Save-SmokeSnapshot $Name $foreground
        if ($stable.ElapsedMilliseconds -ge 750 -and
            (Assert-SmokeTarget $evidence $Process $Target $instance $generation)) {
            # Keep the same application genuinely foreground; prove stable target dedup over > debounce.
            $attempt = $evidence.activation_attempt; $sequence = $evidence.decision_sequence
            Start-Sleep -Milliseconds 1200
            $next = Save-SmokeSnapshot $Name ([AuraSmokeForeground]::Basename())
            if ((Assert-SmokeTarget $next $Process $Target $instance $generation) -and
                $next.activation_attempt -eq $attempt -and $next.decision_sequence -eq $sequence) {
                if ($next.activation_attempt - $start.activation_attempt -gt 1) { throw 'More than one activation attempt occurred in this foreground stage.' }
                $summary.stages += @{ name = $Name; outcome = 'passed'; duplicate_suppression = $true; evidence = "$Name.json" }
                return
            }
            throw 'Foreground changed or duplicate activation/decision occurred during stable observation.'
        }
        Start-Sleep -Milliseconds 250
    }
    throw "Timeout: real foreground/target could not be confirmed for $Name. No physical PASS claimed."
}
try {
    $before = Save-SmokeSnapshot 'before'
    $instance = $before.daemon.process_instance_id; $generation = $before.session_generation
    if (-not $instance) { throw 'Daemon process identity missing.' }
    if ($AllowHardwareWrites) {
        if (-not $IsWindows) { throw 'Local hardware smoke requires Windows.' }
        $a = [Guid]::Parse($ProfileA).ToString(); $b = [Guid]::Parse($ProfileB).ToString()
        if ($a -eq $b) { throw 'ProfileA and ProfileB must be distinct stable GUIDs.' }
        if ($before.persistent_safety_quarantine -or $before.m605_health -ne 'Clean') { throw 'M605 is not clean; smoke does not recover/clear it.' }
        $config = Invoke-SmokeApi '/automation'
        if (-not $config.automation_configuration_available) { throw 'Automation configuration unavailable; retained.' }
        foreach ($id in @($a, $b)) {
            if ($id -notin $config.profiles.id) { throw 'A target Profile is missing.' }
        }
        if ($before.manual_hold) { throw 'An existing ManualHold is active. Change foreground normally before starting; smoke will not fake/clear it.' }
        $original = $config.device_profile_automation
        if ($original.schema_version -ne 1) { throw 'Unsupported automation schema; no mutation.' }
        $summary.original_automation_enabled = $original.enabled
        $temporary = ($original | ConvertTo-Json -Depth 100 | ConvertFrom-Json -AsHashtable)
        # No fallback or existing binding is changed. Refuse conflicting max-priority rules.
        foreach ($rule in $temporary.bindings) {
            if ($rule.enabled -and $rule.process_name.ToLowerInvariant() -in @('notepad.exe', 'mspaint.exe') -and $rule.priority -eq [int]::MaxValue) {
                throw 'Existing maximum-priority Notepad/Paint rule conflicts; not overwritten.'
            }
        }
        $temporary.enabled = $true
        foreach ($pair in @(@('notepad.exe', $a), @('mspaint.exe', $b))) {
            $ruleId = [Guid]::NewGuid().ToString(); $summary.temporary_rule_ids += $ruleId
            $temporary.bindings += @{ rule_id = $ruleId; enabled = $true; process_name = $pair[0]; profile_id = $pair[1]; priority = [int]::MaxValue }
        }
        # A response timeout may follow a successful commit. finally inspects IDs/config rather than assuming failure is nonmutating.
        $installed = $temporary; $restoreRequired = $true
        $updated = Invoke-SmokeApi '/automation' @{ expected_revision = $config.document_revision; device_profile_automation = $temporary }
        $installed = $updated.device_profile_automation
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Diagnostics;
public static class AuraSmokeForeground {
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    public static string Basename() {
        try {
            uint process; var window = GetForegroundWindow();
            if (window == IntPtr.Zero) return null;
            GetWindowThreadProcessId(window, out process);
            return Process.GetProcessById((int)process).ProcessName.ToLowerInvariant() + ".exe";
        } catch { return null; }
    }
}
'@
        Wait-SmokeTarget 'notepad-a' 'notepad.exe' $a
        Wait-SmokeTarget 'paint-b' 'mspaint.exe' $b
        Wait-SmokeTarget 'notepad-return-a' 'notepad.exe' $a
        if ($ManualHold) {
            Write-Host '保持 Notepad 前台，在 Aura 手动 Apply Profile B，然后返回 Notepad。保存不算手动 Apply。'
            Read-Host '完成后按 Enter（脚本将等你重新切回 Notepad）' | Out-Null
            $deadline = [DateTime]::UtcNow.AddSeconds($StageTimeoutSeconds)
            $held = $false
            do {
                $snapshot = Save-SmokeSnapshot 'manual-hold' ([AuraSmokeForeground]::Basename())
                if ($snapshot.daemon.process_instance_id -ne $instance -or $snapshot.persistent_safety_quarantine) { throw 'ManualHold interrupted by daemon restart/quarantine.' }
                if ($snapshot.actual_foreground -eq 'notepad.exe' -and $snapshot.manual_hold -and
                    $snapshot.manual_hold_foreground -eq 'notepad.exe' -and $snapshot.selected_profile_id -eq $b) { $held = $true; break }
                Start-Sleep -Milliseconds 250
            } while ([DateTime]::UtcNow -lt $deadline)
            if (-not $held) { throw 'A genuine manual action/Notepad hold could not be observed.' }
            Start-Sleep -Milliseconds 1200
            $next = Save-SmokeSnapshot 'manual-hold' ([AuraSmokeForeground]::Basename())
            if ($next.actual_foreground -ne 'notepad.exe' -or -not $next.manual_hold -or
                $next.selected_profile_id -ne $b -or $next.activation_attempt -ne $snapshot.activation_attempt) { throw 'ManualHold did not suppress automation.' }
            $summary.stages += @{ name = 'manual-hold'; outcome = 'passed'; evidence = 'manual-hold.json' }
            Wait-SmokeTarget 'hold-clear-paint' 'mspaint.exe' $b
            Wait-SmokeTarget 'hold-return-notepad' 'notepad.exe' $a
        }
    } else {
        Write-Host 'Read-only diagnostics collected. No foreground app, configuration mutation or switching was requested.'
    }
    $summary.overall = 'passed'
} catch {
    $summary.overall = 'failed'
    # Bounded generic message only: never export HTTP response bodies or full paths.
    $summary.error = 'Smoke failed; inspect console and sanitized stage evidence.'
    Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
    if ($restoreRequired) {
        try {
            $current = Invoke-SmokeApi '/automation'
            $identity = Save-SmokeSnapshot 'before-restore'
            if ($identity.daemon.process_instance_id -ne $instance) { throw 'Daemon restarted; refusing to overwrite a new process configuration.' }
            if (Test-SmokeConfigEqual $current.device_profile_automation $original) {
                $summary.restoration = 'unchanged'
            } elseif (Test-SmokeConfigEqual $current.device_profile_automation $installed) {
                $null = Invoke-SmokeApi '/automation' @{ expected_revision = $current.document_revision; device_profile_automation = $original }
                $verified = Invoke-SmokeApi '/automation'
                if (-not (Test-SmokeConfigEqual $verified.device_profile_automation $original)) { throw 'Restore verification mismatch.' }
                $summary.restoration = 'restored'
            } else { throw 'Concurrent automation edit: original not overwritten. Remove temporary rule GUIDs from summary and review original enabled flag.' }
        } catch {
            $summary.restoration = 'failed'; $summary.overall = 'failed'
            Write-Host "RESTORE FAILED: $($_.Exception.Message)" -ForegroundColor Red
        }
    }
    try { $null = Save-SmokeSnapshot 'final' } catch { $summary.overall = 'failed' }
    $summary.completed_at_utc = [DateTime]::UtcNow.ToString('o')
    $summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $output 'hardware-smoke-summary.json') -Encoding utf8
    # Only this tool's sanitized JSON is bundled, never raw daemon diagnostics/config.
    Compress-Archive -Path (Join-Path $output '*.json') -DestinationPath "$output.zip" -Force
}
if ($summary.overall -ne 'passed') { exit 1 }
