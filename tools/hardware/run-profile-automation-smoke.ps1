<#
.SYNOPSIS
    Runs local hardware profile automation smoke validation.
.DESCRIPTION
    Validates foreground-driven profile switching on local hardware.
    In read-only mode (default), queries daemon diagnostics without modifying configuration or switching profiles.
    When -AllowHardwareWrites is specified, temporarily binds Program A and Program B to Profile A and Profile B,
    verifies foreground application switching across A -> B -> A, and restores original configuration in finally.
.PARAMETER ProfileA
    Profile name or stable GUID for Program A.
.PARAMETER ProfileB
    Profile name or stable GUID for Program B.
.PARAMETER ProgramA
    Foreground test executable for Profile A (default: notepad.exe).
.PARAMETER ProgramB
    Foreground test executable for Profile B (default: charmap.exe).
.PARAMETER AllowHardwareWrites
    Explicit opt-in required to write temporary automation bindings and execute hardware profile switches.
.PARAMETER ManualHold
    Optional stage to verify manual profile application hold.
.PARAMETER DaemonUrl
    Daemon HTTP origin (default: http://127.0.0.1:19897).
.PARAMETER OutputDirectory
    Directory to save smoke evidence and summary.
.PARAMETER StageTimeoutSeconds
    Timeout per foreground stage in seconds (default: 120).
.EXAMPLE
    pwsh ./tools/hardware/run-profile-automation-smoke.ps1
.EXAMPLE
    pwsh ./tools/hardware/run-profile-automation-smoke.ps1 `
      -ProfileA "Desktop" `
      -ProfileB "Gaming" `
      -AllowHardwareWrites
.EXAMPLE
    pwsh ./tools/hardware/run-profile-automation-smoke.ps1 `
      -ProfileA "56c39b60-f6b0-448f-b107-6a714ccb0dd1" `
      -ProfileB "a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d" `
      -AllowHardwareWrites
#>
#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$ProfileA,
    [string]$ProfileB,
    [string]$ProgramA = 'notepad.exe',
    [string]$ProgramB = 'charmap.exe',
    [switch]$AllowHardwareWrites,
    [switch]$ManualHold,
    [ValidateRange(0,5)][int]$ExpectedHardwareSlotA = 0,
    [ValidateRange(0,5)][int]$ExpectedHardwareSlotB = 0,
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
    profile_a = $null; profile_b = $null
    program_a = $null; program_b = $null
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
    if ($ExpectedHardwareSlotA -or $ExpectedHardwareSlotB) {
        $slot = $response.diagnostics.hardware_slot_status
        $evidence.hardware_slot = [ordered]@{
            desired = $slot.desired_hardware_slot; observed = $slot.observed_hardware_slot
            match = $slot.hardware_slot_match; observed_at_utc = $slot.slot_observation_time; source = $slot.source
            selector_count = $slot.selector_count
        }
    }
    $evidence | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $output "$Name.json") -Encoding utf8
    return $evidence
}
function Wait-SmokeTarget {
    param([string]$Name, [string]$Process, [string]$Target, [string]$ExecutablePath = $Process, [int]$ExpectedSlot = 0)
    $start = Save-SmokeSnapshot "$Name-start" ([AuraSmokeForeground]::Basename())
    # Users focus the real application. SetForegroundWindow is deliberately not faked or forced.
    Write-Host "请打开并切换到 $Process，保持前台直到本阶段完成。" -ForegroundColor Cyan
    Start-Process -FilePath $ExecutablePath -WindowStyle Normal | Out-Null
    $timer = [Diagnostics.Stopwatch]::StartNew(); $stable = [Diagnostics.Stopwatch]::new()
    while ($timer.Elapsed.TotalSeconds -lt $StageTimeoutSeconds) {
        $foreground = [AuraSmokeForeground]::Basename()
        if ($foreground -eq $Process) {
            if (-not $stable.IsRunning) { $stable.Restart() }
        } else { $stable.Reset() }
        $evidence = Save-SmokeSnapshot $Name $foreground
        if ($stable.ElapsedMilliseconds -ge 750 -and
            (Assert-SmokeTarget $evidence $Process $Target $instance $generation)) {
            if ($ExpectedSlot) {
                # Query immediately before acceptance; cached submission is insufficient.
                $null = Invoke-SmokeApi '/hardware-slot/refresh' @{}
                $evidence = Save-SmokeSnapshot $Name ([AuraSmokeForeground]::Basename())
                if (-not (Assert-SmokeTarget $evidence $Process $Target $instance $generation) -or
                    $evidence.hardware_slot.observed -ne $ExpectedSlot -or -not $evidence.hardware_slot.match) {
                    throw 'Actual hardware slot does not match foreground target.'
                }
            }
            # Keep the same application genuinely foreground; prove stable target dedup over > debounce.
            $accepted = $evidence
            if ($ExpectedSlot -and $evidence.activation_decision_sequence -ne $evidence.decision_sequence) {
                throw 'Target state was not produced by the current accepted decision.'
            }
            Start-Sleep -Milliseconds 1200
            if ($ExpectedSlot) { $null = Invoke-SmokeApi '/hardware-slot/refresh' @{} }
            $next = Save-SmokeSnapshot $Name ([AuraSmokeForeground]::Basename())
            if (Assert-SmokeTarget $next $Process $Target $instance $generation) {
                Assert-SmokeStableDecision $accepted $next -HardwareSlot:([bool]$ExpectedSlot)
                if ($ExpectedSlot -and ($next.hardware_slot.observed -ne $ExpectedSlot -or -not $next.hardware_slot.match)) {
                    throw 'External slot drift detected; no automatic recovery.'
                }
                if ($ExpectedSlot -and $next.hardware_slot.selector_count - $start.hardware_slot.selector_count -gt 1) {
                    throw 'More than one hardware selector occurred in this foreground stage.'
                }
                $summary.stages += @{ name = $Name; outcome = 'passed'; duplicate_suppression = $true;
                    decision_identity = (Get-SmokeDecisionIdentity $accepted); evidence = "$Name.json" }
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
        if ($before.persistent_safety_quarantine -or $before.m605_health -ne 'Clean') { throw 'M605 is not clean; smoke does not recover/clear it.' }
        if ($before.manual_hold) { throw 'An existing ManualHold is active. Change foreground normally before starting; smoke will not fake/clear it.' }

        # Resolve executables before any mutation
        $resolvedExeA = Resolve-SmokeProgram $ProgramA 'ProgramA'
        $resolvedExeB = Resolve-SmokeProgram $ProgramB 'ProgramB'
        $progABasename = [System.IO.Path]::GetFileName($resolvedExeA).ToLowerInvariant()
        $progBBasename = [System.IO.Path]::GetFileName($resolvedExeB).ToLowerInvariant()
        if ($progABasename -eq $progBBasename) {
            throw "Program A and Program B resolve to the same executable basename ('$progABasename'). Choose two distinct programs."
        }

        # Query automation & profiles via daemon API
        $config = Invoke-SmokeApi '/automation'
        if (-not $config.automation_configuration_available) { throw 'Automation configuration unavailable; retained.' }
        $profileAResolved = Resolve-SmokeProfile $ProfileA 'ProfileA' $config.profiles
        $profileBResolved = Resolve-SmokeProfile $ProfileB 'ProfileB' $config.profiles
        if ($profileAResolved.resolved_id -eq $profileBResolved.resolved_id) {
            throw 'Profile A and Profile B resolve to the same profile. Choose two different profiles for the switching smoke test.'
        }
        $a = $profileAResolved.resolved_id
        $b = $profileBResolved.resolved_id
        if ($ExpectedHardwareSlotA -or $ExpectedHardwareSlotB) {
            foreach ($pair in @(@($a,$ExpectedHardwareSlotA), @($b,$ExpectedHardwareSlotB))) {
                $profile = @($config.profiles | Where-Object { $_.id -eq $pair[0] })[0]
                if ($profile.activation_backend -ne 'hardware_slot' -or $profile.hardware_slot -ne $pair[1]) {
                    throw 'Hardware smoke requires existing hardware_slot profiles with the requested slots; no bank authoring.'
                }
            }
            $null = Invoke-SmokeApi '/hardware-slot/refresh' @{}
            $baseline = Save-SmokeSnapshot 'basicinfo-baseline'
            $generation = $baseline.session_generation
        }

        # Observe actual foreground processes
        $observedA = Get-SmokeForegroundBasename $resolvedExeA $ProgramA 'ProgramA' $StageTimeoutSeconds
        $observedB = Get-SmokeForegroundBasename $resolvedExeB $ProgramB 'ProgramB' $StageTimeoutSeconds
        if ($observedA -eq $observedB) {
            throw "Program A and Program B resolve to the same foreground process ('$observedA'). Choose two distinct programs."
        }

        # Print hardware smoke plan before any hardware writes
        Write-Host ""
        Write-Host "Hardware smoke plan" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "Program A:"
        Write-Host "  $observedA"
        Write-Host ""
        Write-Host "Profile A:"
        Write-Host "  $($profileAResolved.resolved_name)"
        Write-Host "  $($profileAResolved.resolved_id)"
        Write-Host ""
        Write-Host "Program B:"
        Write-Host "  $observedB"
        Write-Host ""
        Write-Host "Profile B:"
        Write-Host "  $($profileBResolved.resolved_name)"
        Write-Host "  $($profileBResolved.resolved_id)"
        Write-Host ""
        Write-Host "Sequence:"
        Write-Host "  $observedA → $observedB → $observedA"
        Write-Host ""

        # Populate resolution metadata in summary
        $summary.profile_a = [ordered]@{
            requested     = $profileAResolved.requested
            resolved_name = $profileAResolved.resolved_name
            resolved_id   = $profileAResolved.resolved_id
        }
        $summary.profile_b = [ordered]@{
            requested     = $profileBResolved.requested
            resolved_name = $profileBResolved.resolved_name
            resolved_id   = $profileBResolved.resolved_id
        }
        $summary.program_a = [ordered]@{
            requested           = [System.IO.Path]::GetFileName($ProgramA)
            observed_foreground = $observedA
        }
        $summary.program_b = [ordered]@{
            requested           = [System.IO.Path]::GetFileName($ProgramB)
            observed_foreground = $observedB
        }

        $original = $config.device_profile_automation
        if ($original.schema_version -ne 1) { throw 'Unsupported automation schema; no mutation.' }
        $summary.original_automation_enabled = $original.enabled
        $temporary = ($original | ConvertTo-Json -Depth 100 | ConvertFrom-Json -AsHashtable)
        # No fallback or existing binding is changed. Refuse conflicting max-priority rules.
        foreach ($rule in $temporary.bindings) {
            if ($rule.enabled -and $rule.process_name.ToLowerInvariant() -in @($observedA, $observedB) -and $rule.priority -eq [int]::MaxValue) {
                throw "Existing maximum-priority rule for '$($rule.process_name)' conflicts; not overwritten."
            }
        }
        $temporary.enabled = $true
        foreach ($pair in @(@($observedA, $a), @($observedB, $b))) {
            $ruleId = [Guid]::NewGuid().ToString(); $summary.temporary_rule_ids += $ruleId
            $temporary.bindings += @{ rule_id = $ruleId; enabled = $true; process_name = $pair[0]; profile_id = $pair[1]; priority = [int]::MaxValue }
        }
        # A response timeout may follow a successful commit. finally inspects IDs/config rather than assuming failure is nonmutating.
        $installed = $temporary; $restoreRequired = $true
        # Discovery of B leaves B foreground. Re-establish A before installing
        # either binding, then verify continuously over the daemon debounce.
        $null = Get-SmokeForegroundBasename $resolvedExeA $ProgramA 'ProgramA' $StageTimeoutSeconds
        $stablePre = [Diagnostics.Stopwatch]::StartNew()
        while ($stablePre.ElapsedMilliseconds -lt 750) {
            if ([AuraSmokeForeground]::Basename() -ne $observedA) { throw 'Program A lost foreground before binding installation.' }
            Start-Sleep -Milliseconds 100
        }
        $updated = Invoke-SmokeApi '/automation' @{ expected_revision = $config.document_revision; device_profile_automation = $temporary }
        $installed = $updated.device_profile_automation

        $stageA = [System.IO.Path]::GetFileNameWithoutExtension($observedA)
        $stageB = [System.IO.Path]::GetFileNameWithoutExtension($observedB)
        Wait-SmokeTarget "$stageA-a" $observedA $a $resolvedExeA $ExpectedHardwareSlotA
        Wait-SmokeTarget "$stageB-b" $observedB $b $resolvedExeB $ExpectedHardwareSlotB
        Wait-SmokeTarget "$stageA-return-a" $observedA $a $resolvedExeA $ExpectedHardwareSlotA
        if ($ManualHold) {
            Write-Host "保持 $observedA 前台，在 Aura 手动 Apply Profile B，然后返回 $observedA。保存不算手动 Apply。"
            Read-Host "完成后按 Enter（脚本将等你重新切回 $observedA）" | Out-Null
            $deadline = [DateTime]::UtcNow.AddSeconds($StageTimeoutSeconds)
            $held = $false
            do {
                $snapshot = Save-SmokeSnapshot 'manual-hold' ([AuraSmokeForeground]::Basename())
                if ($snapshot.daemon.process_instance_id -ne $instance -or $snapshot.persistent_safety_quarantine) { throw 'ManualHold interrupted by daemon restart/quarantine.' }
                if ($snapshot.actual_foreground -eq $observedA -and $snapshot.manual_hold -and
                    $snapshot.manual_hold_foreground -eq $observedA -and $snapshot.selected_profile_id -eq $b) { $held = $true; break }
                Start-Sleep -Milliseconds 250
            } while ([DateTime]::UtcNow -lt $deadline)
            if (-not $held) { throw "A genuine manual action/$observedA hold could not be observed." }
            if ($ExpectedHardwareSlotB) {
                $null = Invoke-SmokeApi '/hardware-slot/refresh' @{}
                $snapshot = Save-SmokeSnapshot 'manual-hold' ([AuraSmokeForeground]::Basename())
                if ($snapshot.hardware_slot.observed -ne $ExpectedHardwareSlotB -or -not $snapshot.hardware_slot.match) {
                    throw 'ManualHold target is not the actual hardware slot.'
                }
            }
            Start-Sleep -Milliseconds 1200
            if ($ExpectedHardwareSlotB) { $null = Invoke-SmokeApi '/hardware-slot/refresh' @{} }
            $next = Save-SmokeSnapshot 'manual-hold' ([AuraSmokeForeground]::Basename())
            if ($next.actual_foreground -ne $observedA -or -not $next.manual_hold -or
                $next.selected_profile_id -ne $b) { throw 'ManualHold did not suppress automation.' }
            Assert-SmokeStableDecision $snapshot $next -HardwareSlot:([bool]$ExpectedHardwareSlotB)
            if ($ExpectedHardwareSlotB -and ($next.hardware_slot.observed -ne $ExpectedHardwareSlotB -or -not $next.hardware_slot.match)) {
                throw 'Actual slot changed during ManualHold.'
            }
            $summary.stages += @{ name = 'manual-hold'; outcome = 'passed'; evidence = 'manual-hold.json' }
            Wait-SmokeTarget "hold-clear-$stageB" $observedB $b $resolvedExeB $ExpectedHardwareSlotB
            Wait-SmokeTarget "hold-return-$stageA" $observedA $a $resolvedExeA $ExpectedHardwareSlotA
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
