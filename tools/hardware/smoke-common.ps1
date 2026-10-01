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
        selected_profile_id = $r.selected_profile_id; active_profile_id = $r.active_profile_id; dirty = $r.dirty
        document_revision = $r.document_revision; runtime_revision = $r.runtime_revision; mutation_revision = $r.mutation_revision
        persistent_safety_quarantine = $m.persistent_safety_quarantine; m605_health = $m.health
        session_generation = $m.session_generation
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
