Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../ci/ci-common.ps1"

function Assert-LocalSmokePermission {
    param([switch]$AllowHardwareWrites, [string]$DaemonUrl)
    if ($AllowHardwareWrites -and (Test-CiEnvironment)) {
        throw 'Hardware acceptance/write mode is forbidden when CI or GITHUB_ACTIONS is set.'
    }
    $uri = [Uri]$DaemonUrl
    if ($uri.Scheme -ne 'http' -or $uri.Host -notin @('127.0.0.1', 'localhost', '[::1]') -or
        $uri.UserInfo -or $uri.Query -or $uri.Fragment -or $uri.AbsolutePath -ne '/') {
        throw 'Smoke uses only a local daemon origin, without credentials/path/query.'
    }
}

function Get-SmokeEvidence {
    param([System.Collections.IDictionary]$Diagnostics, [string]$ActualForeground)
    # Explicit allowlist: no Profile names/config, arbitrary extensions, paths, PID, process lists or raw HID.
    $d = $Diagnostics
    $r = $d.runtime; $a = $d.automation; $m = $d.m605
    return [ordered]@{
        schema_version = 1; captured_at_utc = [DateTime]::UtcNow.ToString('o')
        daemon = [ordered]@{ process_instance_id = $d.daemon.process_instance_id; product_version = $d.daemon.product_version }
        actual_foreground = $ActualForeground; foreground_process = $a.foreground_process
        debounce_pending = $a.debounce_pending; debounce_ms = $a.debounce_ms
        decision_sequence = $a.decision_sequence; resolved_profile_id = $a.resolved_profile_id
        manual_hold = $a.manual_hold; manual_hold_foreground = $a.manual_hold_foreground
        activation_attempt = $a.activation_attempt; activation_outcome = $a.activation_outcome
        activation_target = $a.activation_target; activation_state = $a.activation_state
        activation_decision_sequence = $(if ($a.Contains('activation_decision_sequence')) { $a.activation_decision_sequence } else { $null })
        configuration_revision = $(if ($a.Contains('configuration_document_revision')) { $a.configuration_document_revision } else { $null })
        manual_action_sequence = $(if ($a.Contains('manual_action_sequence')) { $a.manual_action_sequence } else { $null })
        foreground_observation_sequence = $(if ($a.Contains('foreground_observation_sequence')) { $a.foreground_observation_sequence } else { $null })
        selected_profile_id = $r.selected_profile_id; active_profile_id = $r.active_profile_id; dirty = $r.dirty
        document_revision = $r.document_revision; runtime_revision = $r.runtime_revision; mutation_revision = $r.mutation_revision
        persistent_safety_quarantine = $m.persistent_safety_quarantine; m605_health = $m.health
        session_generation = $m.session_generation
    }
}

function Get-SmokeDecisionIdentity($Evidence) {
    return (@($Evidence.decision_sequence, $Evidence.configuration_revision,
        $Evidence.manual_action_sequence, $Evidence.foreground_observation_sequence,
        $Evidence.resolved_profile_id) | ConvertTo-Json -Compress)
}

function Assert-SmokeStableDecision($Before, $After, [switch]$HardwareSlot) {
    if ((Get-SmokeDecisionIdentity $Before) -cne (Get-SmokeDecisionIdentity $After)) {
        throw 'Accepted foreground decision changed during stable observation.'
    }
    if ($HardwareSlot) {
        if ($After.hardware_slot.selector_count -ne $Before.hardware_slot.selector_count) {
            throw 'Duplicate hardware selector during one stable decision.'
        }
    } elseif ($After.activation_attempt -ne $Before.activation_attempt) {
        throw 'Duplicate host-managed activation during stable decision.'
    }
}

function ConvertTo-SmokeCanonical {
    param($Value)
    if ($Value -is [System.Collections.IDictionary]) {
        $sorted = [ordered]@{}
        foreach ($key in ($Value.Keys | Sort-Object)) { $sorted[$key] = ConvertTo-SmokeCanonical $Value[$key] }
        return $sorted
    }
    if ($Value -is [System.Collections.IList]) {
        $array = @(); foreach ($item in $Value) { $array += ,(ConvertTo-SmokeCanonical $item) }
        return ,$array
    }
    return $Value
}

function Test-SmokeConfigEqual {
    param($Left, $Right)
    return ((ConvertTo-SmokeCanonical $Left | ConvertTo-Json -Depth 100 -Compress) -ceq
        (ConvertTo-SmokeCanonical $Right | ConvertTo-Json -Depth 100 -Compress))
}

function Assert-SmokeTarget {
    param($Evidence, [string]$Process, [string]$Target, [string]$Instance, $Generation)
    if ($Evidence.daemon.process_instance_id -ne $Instance) { throw 'Daemon restarted during smoke.' }
    if ($Evidence.session_generation -ne $Generation) { throw 'M605 generation changed; physical reconnect is not covered by this smoke.' }
    if ($Evidence.persistent_safety_quarantine) { throw 'M605 quarantined; no acknowledgement/clear is attempted.' }
    return ($Evidence.actual_foreground -eq $Process -and $Evidence.foreground_process -eq $Process -and
        -not $Evidence.debounce_pending -and $Evidence.resolved_profile_id -eq $Target -and
        $Evidence.selected_profile_id -eq $Target -and $Evidence.active_profile_id -eq $Target -and
        -not $Evidence.dirty -and $Evidence.m605_health -eq 'Clean' -and
        $Evidence.activation_outcome -in @('succeeded', 'no-op'))
}

if ($IsWindows -and -not ('AuraSmokeForeground' -as [type])) {
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
}

function Resolve-SmokeProgram {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Program,
        [Parameter(Mandatory = $true)]
        [string]$ParameterName
    )
    if ([string]::IsNullOrWhiteSpace($Program)) {
        throw "Test program parameter -$ParameterName cannot be empty."
    }
    if ($Program -match '[\\/]') {
        if (-not (Test-Path -LiteralPath $Program -PathType Leaf)) {
            throw "Test program '$Program' could not be found. Supply -$ParameterName with another stable executable."
        }
        return [System.IO.Path]::GetFullPath($Program)
    }
    $cmd = Get-Command -Name $Program -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $cmd) {
        if (-not $Program.EndsWith('.exe', [StringComparison]::OrdinalIgnoreCase)) {
            $cmd = Get-Command -Name "$Program.exe" -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        }
    }
    if (-not $cmd) {
        throw "Test program '$Program' could not be found. Supply -$ParameterName with another stable executable."
    }
    return [string]$cmd.Source
}

function Resolve-SmokeProfile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InputProfile,
        [Parameter(Mandatory = $true)]
        [string]$ParameterName,
        [Parameter(Mandatory = $true)]
        $Profiles
    )
    if ([string]::IsNullOrWhiteSpace($InputProfile)) {
        throw "Profile parameter -$ParameterName is required."
    }
    $profileList = @($Profiles)

    $parsedGuid = [Guid]::Empty
    if ([Guid]::TryParse($InputProfile, [ref]$parsedGuid)) {
        $canonicalGuid = $parsedGuid.ToString()
        $matched = @($profileList | Where-Object {
            $pGuid = [Guid]::Empty
            if ($_.id -and [Guid]::TryParse($_.id, [ref]$pGuid)) {
                return $pGuid -eq $parsedGuid
            }
            return $false
        })
        if ($matched.Count -eq 0) {
            $available = @($profileList | ForEach-Object {
                $n = if ($_.name) { $_.name } else { '<unnamed>' }
                "  $n ($($_.id))"
            }) -join "`n"
            throw "Profile GUID '$InputProfile' was not found.`n`nAvailable profiles:`n$available"
        }
        $p = $matched[0]
        $name = if ($p.name) { [string]$p.name } else { $canonicalGuid }
        return [ordered]@{
            requested     = $InputProfile
            resolved_name = $name
            resolved_id   = $canonicalGuid
        }
    }

    $matching = @($profileList | Where-Object {
        $_.name -and [string]::Equals($_.name, $InputProfile, [StringComparison]::OrdinalIgnoreCase)
    })

    if ($matching.Count -gt 1) {
        $duplicates = @($matching | ForEach-Object {
            "  $($_.name) ($($_.id))"
        }) -join "`n"
        throw "Profile name '$InputProfile' is ambiguous.`nMatching profiles:`n$duplicates`nSpecify the stable profile GUID via -$ParameterName."
    }

    if ($matching.Count -eq 0) {
        $available = @($profileList | ForEach-Object {
            $n = if ($_.name) { $_.name } else { '<unnamed>' }
            "  $n"
        }) -join "`n"
        throw "Profile '$InputProfile' was not found.`n`nAvailable profiles:`n$available"
    }

    $p = $matching[0]
    $resolvedGuid = ([Guid]::Parse($p.id)).ToString()
    return [ordered]@{
        requested     = $InputProfile
        resolved_name = [string]$p.name
        resolved_id   = $resolvedGuid
    }
}

function Get-SmokeForegroundBasename {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath,
        [Parameter(Mandatory = $true)]
        [string]$Requested,
        [Parameter(Mandatory = $true)]
        [string]$ParameterName,
        [int]$TimeoutSeconds = 120
    )
    $expectedBasename = [System.IO.Path]::GetFileName($ExecutablePath).ToLowerInvariant()
    $shellForeground = [AuraSmokeForeground]::Basename()
    Write-Host "启动并验证 $Requested 前台进程（预期: $expectedBasename）..." -ForegroundColor Cyan
    try {
        Start-Process -FilePath $ExecutablePath -WindowStyle Normal | Out-Null
    } catch {
        throw "Test program '$Requested' could not be launched. Supply -$ParameterName with another stable executable."
    }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $observed = $null
    while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $current = [AuraSmokeForeground]::Basename()
        if ($current) {
            if ($current -eq $expectedBasename) {
                $observed = $current
                break
            }
            # A different window is not proof of this executable's foreground.
        }
        Start-Sleep -Milliseconds 200
    }
    if (-not $observed) {
        if ($shellForeground -eq $expectedBasename) {
            $observed = $expectedBasename
        } else {
            throw "Test program '$Requested' could not be observed in the foreground within $TimeoutSeconds seconds. Supply -$ParameterName with another stable executable."
        }
    }
    if ($observed -ne $expectedBasename) {
        Write-Host "Notice: requested executable was '$Requested', but observed foreground executable is '$observed'." -ForegroundColor Yellow
    }
    return $observed
}
