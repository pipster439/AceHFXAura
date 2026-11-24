#Requires -Version 7.0
[CmdletBinding()]
param(
    [switch]$Apply,
    [ValidateRange(1,36500)][int]$OlderThanDays = 7,
    [string]$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')),
    [string]$OutputJson
)
. "$PSScriptRoot/generated-common.ps1"
$context = Get-AuraSafetyContext $RepoRoot
$common = (& git -C $context.repo rev-parse --path-format=absolute --git-common-dir).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Git common directory lookup failed' }
$registryPath = Join-Path $common 'aura-temporary-worktrees.json'
$owned = @()
if (Test-Path -LiteralPath $registryPath) {
    Assert-AuraNoReparse $common
    $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json -AsHashtable
    if ($registry.schema_version -ne 1 -or $registry.owner -ne 'Aura.dev-worktrees') { throw 'Invalid worktree registry' }
    $owned = @($registry.worktrees)
}
$results = [Collections.Generic.List[object]]::new()
foreach ($worktree in $context.worktrees) {
    $decision = @{path=$worktree;classification='UNKNOWN';action='report-only';reason='No positive temporary-worktree ownership record'}
    try {
        $record = @($owned | Where-Object { (Get-AuraFullPath $_.path) -eq $worktree })
        if ($record.Count -ne 1) { throw $decision.reason }
        $record = $record[0]
        $root = Join-Path $context.repo 'build/dev-worktrees'
        if ((Split-Path $worktree -Parent) -ne $root -or -not (Test-AuraUnder $worktree $root)) { throw 'Outside temporary worktree allowlist' }
        Assert-AuraNoReparse $worktree
        $null = Get-AuraTree $worktree
        $decision.classification = 'EPHEMERAL'
        if ($record.status -ne 'completed' -or -not $record.completed_utc -or
            [DateTime]::Parse($record.completed_utc).ToUniversalTime() -ge [DateTime]::UtcNow.AddDays(-$OlderThanDays)) { throw 'Active/recent worktree protected' }
        if ($record.pid -gt 0 -and (Get-Process -Id $record.pid -ErrorAction SilentlyContinue)) { throw 'Owner process active' }
        foreach ($cwd in $context.cwds) { if ($cwd -eq $worktree -or (Test-AuraUnder $cwd $worktree)) { throw 'Working directory protected' } }
        foreach ($process in $context.processes) {
            $relative = [IO.Path]::GetRelativePath($context.repo, $worktree).Replace('/','\')
            if (($process.ExecutablePath -and (Test-AuraUnder $process.ExecutablePath $worktree)) -or
                ($process.ProcessId -ne $PID -and $process.CommandLine -and
                    ($process.CommandLine.Replace('/','\').Contains($worktree,[StringComparison]::OrdinalIgnoreCase) -or
                     $process.CommandLine.Replace('/','\').Contains($relative,[StringComparison]::OrdinalIgnoreCase)))) { throw 'Active worktree process protected' }
        }
        $dirty = @(& git -C $worktree status --porcelain --untracked-files=all --ignored)
        if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw 'Dirty/untracked/ignored contents protected; report only' }
        $decision.action = $Apply ? 'removed' : 'would-remove'
        $decision.reason = 'Positively registered, completed, stale, completely clean temporary worktree'
        if ($Apply) {
            # Git performs its own final clean/locked-worktree checks. Never --force, never global prune.
            & git -C $context.repo worktree remove -- $worktree
            if ($LASTEXITCODE -ne 0) { throw 'git worktree remove refused; retained' }
        }
    } catch { $decision.action = 'report-only'; $decision.reason = $_.Exception.Message }
    $results.Add($decision)
    Write-Host "$($decision.action): $worktree | $($decision.reason)"
}
if ($OutputJson) { @{schema_version=1;mode=$Apply ? 'apply' : 'dry-run';worktrees=@($results)} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputJson -Encoding utf8 }
