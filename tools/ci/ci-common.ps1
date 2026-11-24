# Shared stage accounting. No product/runtime dependency.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-CiEnvironment {
    # Presence of any non-false CI marker is conservative, including nonstandard runners.
    foreach ($name in @('CI', 'GITHUB_ACTIONS')) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value -and $value.ToLowerInvariant() -notin @('false', '0')) { return $true }
    }
    return $false
}

function Invoke-CiCommand {
    param([Parameter(Mandatory)][string]$File, [string[]]$Arguments = @())
    # npm.ps1 reparses caller source with Invoke-Expression in some npm versions.
    # The installed cmd shim accepts the explicit argument array unchanged.
    if ($IsWindows -and $File -eq 'npm') { $File = (Get-Command npm.cmd -ErrorAction Stop).Source }
    $global:LASTEXITCODE = 0
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        $failure = [Exception]::new("$File exited with code $LASTEXITCODE")
        $failure.Data['ExitCode'] = $LASTEXITCODE
        throw $failure
    }
}

function Write-CiSummary {
    param([System.Collections.IDictionary]$Summary, [string]$Directory)
    $Summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Directory 'ci-summary.json') -Encoding utf8
    $table = @("## Software CI: $($Summary.overall)", '', '| Stage | Result | Duration |', '|---|---|---|')
    foreach ($stage in $Summary.stages) {
        $table += "| $($stage.name) | $($stage.outcome) | $([Math]::Round($stage.duration_ms / 1000, 2)) s |"
    }
    if ($Summary.Contains('storage')) {
        $storage = $Summary.storage
        $table += @('', '### Storage', '', "Mode: $($storage.mode). $($storage.policy)")
        if ($storage.mode -eq 'local') {
            $table += @('', '| Measurement | GiB |', '|---|---|')
            foreach ($key in @('free_bytes_before','estimated_required_bytes','generated_bytes_before','approximate_build_bytes','generated_bytes_after','reclaimed_logical_bytes','free_bytes_after')) {
                $value = $storage[$key]
                $table += "| $key | $(if ($null -eq $value) { 'pending' } else { [Math]::Round($value/1GB,3) }) |"
            }
            $table += @('', "Retained runs (including protected/unknown): $($storage.retained_run_count); artifact entries: $($storage.retained_artifact_entries). $($storage.measurement)")
            $table += "Cleanup errors: $($storage.cleanup_error_count). Decisions: $($storage.cleanup_records -join ', ')."
            if ($storage.Contains('cleanup_error')) { $table += "Cleanup error: $($storage.cleanup_error)" }
        }
    }
    $table | Set-Content -LiteralPath (Join-Path $Directory 'step-summary.md') -Encoding utf8
}

function Invoke-CiStage {
    param([System.Collections.IDictionary]$Summary, [string]$Directory,
        [string]$Name, [string]$Log, [scriptblock]$Action, [switch]$Skip)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $result = $Summary.stages | Where-Object { $_.name -eq $Name } | Select-Object -First 1
    if ($null -eq $result) {
        $result = [ordered]@{ name = $Name; outcome = 'running'; duration_ms = 0; exit_code = $null; log = $Log }
        $Summary.stages.Add($result)
    }
    $result.outcome = 'running'
    Write-CiSummary $Summary $Directory
    $path = Join-Path $Directory $Log
    New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null
    try {
        if ($Skip) {
            $result.outcome = 'skipped'
            'Explicit optional stage skip; this is not a complete required CI run.' | Set-Content -LiteralPath $path
        } else {
            Write-Host "[$Name]"
            & $Action 2>&1 | Tee-Object -FilePath $path | Out-Host
            $result.outcome = 'passed'; $result.exit_code = 0
        }
    } catch {
        $result.outcome = 'failed'; $result.exit_code = 1
        if ($_.Exception.Data.Contains('ExitCode')) { $result.exit_code = $_.Exception.Data['ExitCode'] }
        $_.Exception.Message | Add-Content -LiteralPath $path
        $Summary.overall = 'failed'
        throw
    } finally {
        $timer.Stop(); $result.duration_ms = $timer.ElapsedMilliseconds
        Write-CiSummary $Summary $Directory
    }
}
