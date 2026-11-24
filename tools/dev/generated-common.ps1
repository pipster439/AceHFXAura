#Requires -Version 7.0
# Local generated-output ownership and deletion guards. No hardware/runtime dependency.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-AuraFullPath {
    param([string]$Path)
    if ($Path -match '(^|[\\/])\.\.([\\/]|$)' -or $Path -match '[*?]' -or $Path -match '^[\\/]{2}') {
        throw "Traversal, wildcard or UNC path refused: $Path"
    }
    [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar)
}

function Test-AuraUnder {
    param([string]$Path, [string]$Root)
    $Path.StartsWith($Root.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-AuraNoReparse {
    param([string]$Path)
    for ($entry = Get-Item -LiteralPath $Path -Force; $null -ne $entry; $entry = $entry -is [IO.FileInfo] ? $entry.Directory : $entry.Parent) {
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse path refused: $($entry.FullName)" }
    }
}

function Assert-AuraInactiveLease {
    param([string]$Path)
    $lock = Join-Path $Path '.aura-active.lock'
    if (Test-Path -LiteralPath $lock) {
        Assert-AuraNoReparse $lock
        $probe = [IO.File]::Open($lock, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
        $probe.Dispose()
    }
}

function Get-AuraTree {
    param([string]$Path)
    $bytes = 0L; $newest = [DateTime]::MinValue; $oldest = [DateTime]::MaxValue
    $files = [Collections.Generic.List[IO.FileInfo]]::new()
    if (-not (Test-Path -LiteralPath $Path)) { return @{bytes=0L;files=$files;newest=$newest;oldest=$oldest} }
    Assert-AuraNoReparse $Path
    $pending = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
    $pending.Push([IO.DirectoryInfo]::new($Path))
    while ($pending.Count) {
        foreach ($entry in $pending.Pop().EnumerateFileSystemInfos()) {
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse descendant refused: $($entry.FullName)" }
            if ($entry -is [IO.DirectoryInfo]) { $pending.Push($entry) }
            else {
                $files.Add($entry); $bytes += $entry.Length
                if ($entry.LastWriteTimeUtc -gt $newest) { $newest = $entry.LastWriteTimeUtc }
                if ($entry.LastWriteTimeUtc -lt $oldest) { $oldest = $entry.LastWriteTimeUtc }
            }
        }
    }
    @{bytes=$bytes;files=$files;newest=$newest;oldest=$oldest}
}

function Get-AuraSafetyContext {
    param([string]$RepoRoot)
    $repo = Get-AuraFullPath $RepoRoot
    Assert-AuraNoReparse $repo
    if (-not (Test-Path -LiteralPath (Join-Path $repo '.git'))) { throw 'Repository identity missing' }
    $worktrees = @(& git -C $repo worktree list --porcelain | Where-Object { $_.StartsWith('worktree ') } |
        ForEach-Object { Get-AuraFullPath $_.Substring(9) })
    if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect active worktrees' }
    # Inaccessible process inventory must fail closed, rather than assuming no active build.
    $processes = @(Get-CimInstance Win32_Process -ErrorAction Stop | Select-Object ProcessId,Name,ExecutablePath,CommandLine)
    @{repo=$repo;worktrees=$worktrees;processes=$processes;cwds=@((Get-AuraFullPath (Get-Location).Path), (Get-AuraFullPath ([Environment]::CurrentDirectory)))}
}

function Assert-AuraDeleteTarget {
    param([string]$Path, [string[]]$AllowedRoots, [hashtable]$Context)
    $target = Get-AuraFullPath $Path
    $allowed = @($AllowedRoots | ForEach-Object { Get-AuraFullPath $_ } |
        Where-Object { (Test-AuraUnder $target $_) -and (Split-Path $target -Parent) -eq $_ })
    if ($allowed.Count -ne 1 -or $target -eq $Context.repo -or $target -eq [IO.Path]::GetPathRoot($target).TrimEnd('\','/')) {
        throw "Not an immediate child of an allowlisted generated root: $target"
    }
    Assert-AuraNoReparse $target
    foreach ($cwd in $Context.cwds) {
        if ($cwd -eq $target -or (Test-AuraUnder $cwd $target)) { throw 'Current working directory protected' }
    }
    foreach ($worktree in $Context.worktrees) {
        if ($worktree -eq $target -or (Test-AuraUnder $worktree $target)) { throw 'Registered worktree protected' }
    }
    foreach ($process in $Context.processes) {
        if ($process.ExecutablePath -and (Test-AuraUnder $process.ExecutablePath $target)) { throw "Active executable protected (PID $($process.ProcessId))" }
        $relativeTarget = [IO.Path]::GetRelativePath($Context.repo, $target).Replace('/','\')
        if ($process.ProcessId -ne $PID -and $process.CommandLine -and
            (($process.CommandLine.Replace('/','\').IndexOf($target.Replace('/','\'), [StringComparison]::OrdinalIgnoreCase) -ge 0) -or
             ($process.CommandLine.Replace('/','\').IndexOf($relativeTarget, [StringComparison]::OrdinalIgnoreCase) -ge 0))) {
            throw "Active command protected (PID $($process.ProcessId))"
        }
    }
    $tree = Get-AuraTree $target
    foreach ($file in $tree.files) {
        $relative = [IO.Path]::GetRelativePath($target, $file.FullName).Replace('\','/')
        if ($file.Name -match '^(config\.json|device-profiles\.json|client-settings\.json|\.git|\.aura-protected)$' -or
            $file.Extension -match '^\.(auraeffect|zip|7z|tar|png|jpg|jpeg|webp|pem|key|pfx|pcap|pcapng|config)$' -or
            $file.Name -match '(?i)(^|[-_])(report|evidence|credentials|secrets)([-_.]|$)' -or
            ($file.Name -match '(?i)(^|[-_])(settings|config)([-_.]|$)' -and
             $file.Extension -notin @('.obj','.tlog','.pch','.ilk'))) {
            throw "Protected/curated/user file found: $($file.FullName)"
        }
        # Exact configure-time generator outputs; arbitrary headers/copied source stay protected.
        # Generator/ABI/review drift fails closed until this maintenance proof is reviewed again.
        $generatedHeaderHashes = @{
            'mta-probe/ProbeAbi.h' = '8ADAB6149C6AFC0E70DFB0461C63D019BF66BE45A1ECF39D54359AB41C56D25B'
            'aura-enumeration-characterizer/ReadOnlyAuraAbi.h' = 'EBF38BC0D3B11E41C321A35CB05BE4D4FA1DDBAEC27367C26ABBC8B6B8EAED02'
            'aura-gate-a-software/ReviewedEvidence.h' = '1798A6CC4D9E605860CA9CFEA3B3B2BDA51363B66D89C42DFD5B7D839F792E89'
        }
        $provedGeneratedHeader = $generatedHeaderHashes.ContainsKey($relative) -and
            (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -eq $generatedHeaderHashes[$relative]
        if ($file.Extension -in @('.cs','.py','.ps1','.cpp','.c','.h','.hpp','.xaml','.ts','.tsx','.jsx') -and
            $relative -notmatch '(^|/)CMakeFiles/[^/]+/CompilerId(CXX|C)/CMake(CXX|C)CompilerId\.(cpp|c)$' -and
            $relative -notmatch '^studio_stage4/studio_(old|continuous|one_shot)\.cpp$' -and $relative -ne 'generated/aura_version.h' -and
            -not $provedGeneratedHeader) {
            throw "Unproven source file protected: $relative"
        }
        if ($file.Extension -eq '.json' -and $file.Name -notin @('.aura-generated.json','ci-summary.json','ctest-discovery.json','cleanup-start.json','cleanup-end.json') -and
            $relative -notmatch '(^|/)CMakeFiles/InstallScripts\.json$') { throw "Unproven JSON data protected: $relative" }
        if ($file.Extension -eq '.md' -and $file.Name -ne 'step-summary.md') { throw "Curated document protected: $relative" }
    }
    if (Test-Path -LiteralPath (Join-Path $target '.git')) { throw 'Git workspace protected' }
    $tree
}

function Read-AuraGeneratedManifest {
    param([string]$Path, [string]$RepoRoot, [string]$Category)
    $file = Join-Path $Path '.aura-generated.json'
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw 'UNKNOWN: ownership manifest missing' }
    $manifest = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json -AsHashtable
    foreach ($key in @('schema_version','owner','repo','path','category','status','created_utc','completed_utc','pid')) {
        if (-not $manifest.ContainsKey($key)) { throw "Invalid ownership manifest: $key missing" }
    }
    if ($manifest.schema_version -ne 1 -or $manifest.owner -ne 'Aura.local-ci' -or
        (Get-AuraFullPath $manifest.repo) -ne (Get-AuraFullPath $RepoRoot) -or
        (Get-AuraFullPath $manifest.path) -ne (Get-AuraFullPath $Path) -or $manifest.category -ne $Category) {
        throw 'UNKNOWN/RELEASE: ownership or category mismatch'
    }
    if ($manifest.status -notin @('passed','failed','partial') -or -not $manifest.completed_utc) { throw 'Active or unfinished run protected' }
    if ($manifest.pid -gt 0 -and (Get-Process -Id $manifest.pid -ErrorAction SilentlyContinue)) { throw 'Owner process still active' }
    $manifest
}

function Get-AuraFreeBytes {
    param([string]$Path)
    ([IO.DriveInfo]::new([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Path)))).AvailableFreeSpace
}

function Write-AuraGeneratedManifest {
    param([string]$Path, [System.Collections.IDictionary]$Manifest)
    $destination = Join-Path $Path '.aura-generated.json'
    $Manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath ($destination + '.tmp') -Encoding utf8
    Move-Item -LiteralPath ($destination + '.tmp') -Destination $destination -Force
}

function Invoke-AuraGeneratedCleanup {
    [CmdletBinding()]
    param([string]$RepoRoot, [int]$KeepLast = 3, [double]$OlderThanDays = 1,
        [switch]$IncludeFailedRuns, [switch]$Apply, [string[]]$LegacyBuildNames = @(), [string[]]$Exclude = @())
    $context = Get-AuraSafetyContext $RepoRoot
    $repo = $context.repo
    $allowed = @((Join-Path $repo 'build/ci'), (Join-Path $repo 'artifacts/ci/runs'))
    $decisions = [Collections.Generic.List[object]]::new()
    $candidates = [Collections.Generic.List[object]]::new()
    $cutoff = [DateTime]::UtcNow.AddDays(-$OlderThanDays)
    foreach ($root in $allowed) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        Assert-AuraNoReparse $root
        $category = $root -eq $allowed[0] ? 'EPHEMERAL' : 'SHORT_RETENTION'
        foreach ($directory in Get-ChildItem -LiteralPath $root -Force -Directory) {
            $target = $directory.FullName
            $tree = $null
            try {
                if ($target -in $Exclude -or $directory.Name -eq 'kept' -or $directory.Name -match '(?i)(release|audit|evidence)') { throw 'Current/kept/release/audit run protected' }
                $tree = Assert-AuraDeleteTarget $target $allowed $context
                Assert-AuraInactiveLease $target
                $manifest = Read-AuraGeneratedManifest $target $repo $category
                $completed = [DateTime]::Parse($manifest.completed_utc).ToUniversalTime()
                if ($manifest.status -eq 'failed' -and -not $IncludeFailedRuns) { throw 'Failed evidence retained (opt-in required)' }
                $candidates.Add(@{path=$target;bytes=$tree.bytes;newest=$tree.newest;completed=$completed;status=$manifest.status;category=$category;legacy=$false})
            } catch { $decisions.Add(@{path=$target;action='skip';bytes=$tree ? $tree.bytes : $null;reason=$_.Exception.Message;category=$category}) }
        }
    }
    foreach ($group in $candidates | Group-Object { "$($_.category)/$($_.status)" }) {
        $ordered = @($group.Group | Sort-Object completed -Descending)
        for ($index=0; $index -lt $ordered.Count; $index++) {
            $candidate = $ordered[$index]
            $keep = $index -lt $KeepLast -or $candidate.completed -ge $cutoff -or $candidate.newest -ge $cutoff
            $decisions.Add(@{path=$candidate.path;bytes=$candidate.bytes;category=$candidate.category;
                action=$keep ? 'keep' : 'candidate';reason=$keep ? 'Count/age retention' : 'Owned inactive run beyond count AND age retention';legacy=$false})
        }
    }
    # Historical build directories are never auto-adopted. Explicit names + strict CMake/output proof only.
    foreach ($name in $LegacyBuildNames | Select-Object -Unique) {
        $target = Join-Path $repo "build/$name"
        $tree = $null
        try {
            if ($name -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]*$' -or $name -match '(?i)(release|package|audit|smoke|ci|worker|mutation|repro|profil)') { throw 'Legacy name is invalid or protected' }
            if ($context.processes.Name -match '^(cmake|MSBuild|cl|link|ctest|dotnet)\.exe$') { throw 'Build/test process active; legacy ownership cannot prove inactivity' }
            $tree = Assert-AuraDeleteTarget $target @((Join-Path $repo 'build')) $context
            $cache = Get-Content -LiteralPath (Join-Path $target 'CMakeCache.txt') -Raw
            if ($cache -notmatch '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' -or (Get-AuraFullPath $Matches[1].Trim()) -ne $repo) { throw 'CMake source ownership mismatch' }
            if ($cache -notmatch '(?m)^CMAKE_CACHEFILE_DIR:INTERNAL=(.+)$' -or (Get-AuraFullPath $Matches[1].Trim()) -ne (Get-AuraFullPath $target)) { throw 'CMake output ownership mismatch' }
            if ($cache -notmatch '(?m)^CMAKE_PROJECT_NAME:STATIC=aura_project\r?$') { throw 'Not an Aura CMake tree' }
            foreach ($file in $tree.files) {
                $relative = [IO.Path]::GetRelativePath($target, $file.FullName).Replace('\','/')
                $generated = $relative -match '^CMakeFiles/' -or $relative -match '^studio_stage4/studio_(old|continuous|one_shot)\.cpp$' -or
                    $relative -match '^generated/' -or $file.Extension -in @('.tlog','.obj','.vcxproj','.filters','.recipe','.lastbuildstate','.exp','.dll','.lib','.exe','.pdb','.ilk','.cmake','.rule','.res','.stamp','.sln','.slnx','.bin','.list','.depend','.rc') -or
                    $file.Name -in @('CMakeCache.txt','CTestTestfile.cmake','LastTest.log','LastTestsFailed.log','CTestCostData.txt')
                if (-not $generated) { throw "Unproven legacy file: $relative" }
            }
            $keep = $tree.newest -ge $cutoff
            $decisions.Add(@{path=(Get-AuraFullPath $target);bytes=$tree.bytes;category='EPHEMERAL';action=$keep ? 'keep' : 'candidate';reason=$keep ? 'Age retention' : 'Explicit legacy CMake/output proof; inactive; age expired';legacy=$true})
        } catch { $decisions.Add(@{path=$target;action='skip';bytes=$tree ? $tree.bytes : $null;reason=$_.Exception.Message;category='UNKNOWN'}) }
    }
    $freeBefore = Get-AuraFreeBytes $repo
    $deleted = 0L; $eligible = 0L
    foreach ($decision in $decisions) {
        Write-Verbose "$($decision.action): $($decision.path) ($($decision.bytes) bytes): $($decision.reason)"
        if ($decision.action -ne 'candidate') { continue }
        $eligible += $decision.bytes
        if (-not $Apply) { $decision.action = 'would-delete'; continue }
        $lease = $null
        try {
            # Revalidate immediately before mutation and acquire the same exclusive run lease used by CI.
            $freshContext = Get-AuraSafetyContext $repo
            $roots = $decision.legacy ? @((Join-Path $repo 'build')) : $allowed
            $tree = Assert-AuraDeleteTarget $decision.path $roots $freshContext
            if ($decision.legacy -and $freshContext.processes.Name -match '^(cmake|MSBuild|cl|link|ctest|dotnet)\.exe$') { throw 'Build/test became active; legacy deletion refused' }
            if (-not $decision.legacy) { $null = Read-AuraGeneratedManifest $decision.path $repo $decision.category }
            $leasePath = Join-Path $decision.path '.aura-active.lock'
            $lease = [IO.File]::Open($leasePath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            # Delete only enumerated literal files, then empty directories; never a broad recursive glob.
            $tree = Assert-AuraDeleteTarget $decision.path $roots (Get-AuraSafetyContext $repo)
            foreach ($file in $tree.files) {
                if ($file.FullName -eq $leasePath) { continue }
                Assert-AuraNoReparse $file.DirectoryName
                if ((Get-Item -LiteralPath $file.FullName -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'File changed to reparse point' }
                Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop
                $deleted += $file.Length
            }
            $lease.Dispose(); $lease = $null
            Remove-Item -LiteralPath $leasePath -Force
            $directories = [Collections.Generic.List[string]]::new()
            $pending = [Collections.Generic.Stack[string]]::new(); $pending.Push($decision.path)
            while ($pending.Count) {
                $directory = $pending.Pop(); $directories.Add($directory)
                foreach ($child in Get-ChildItem -LiteralPath $directory -Force -Directory) { Assert-AuraNoReparse $child.FullName; $pending.Push($child.FullName) }
            }
            foreach ($directory in $directories | Sort-Object Length -Descending) { [IO.Directory]::Delete($directory, $false) }
            $decision.action = 'deleted'
        } catch { $decision.action = 'error'; $decision.reason = $_.Exception.Message }
        finally { if ($lease) { $lease.Dispose() } }
    }
    $retained = @($decisions | Where-Object { $_.action -in @('keep','skip','error') })
    $runNames = @($retained | ForEach-Object { Split-Path $_.path -Leaf } | Select-Object -Unique)
    @{schema_version=1;mode=$Apply ? 'apply' : 'dry-run';generated_roots=$allowed;keep_last=$KeepLast;older_than_days=$OlderThanDays;
        estimated_reclaimable_bytes=$eligible;deleted_logical_bytes=$deleted;free_bytes_before=$freeBefore;free_bytes_after=(Get-AuraFreeBytes $repo);
        retained_run_count=$runNames.Count;retained_artifact_entries=$retained.Count;decisions=@($decisions)}
}

function Get-AuraCiStorageEstimate {
    param([string]$RepoRoot)
    $history = 0L
    $root = Join-Path $RepoRoot 'build/ci'
    if (Test-Path -LiteralPath $root) {
        Assert-AuraNoReparse $root
        foreach ($directory in Get-ChildItem -LiteralPath $root -Directory) {
            $cache = Join-Path $directory.FullName 'CMakeCache.txt'
            if (Test-Path -LiteralPath $cache) {
                Assert-AuraNoReparse $directory.FullName
                Assert-AuraNoReparse $cache
                $text = Get-Content -LiteralPath $cache -Raw
                if ($text -match '(?m)^CMAKE_PROJECT_NAME:STATIC=aura_project\r?$') {
                    $history = [Math]::Max($history, (Get-AuraTree $directory.FullName).bytes)
                }
            }
            $marker = Join-Path $directory.FullName '.aura-generated.json'
            if (Test-Path -LiteralPath $marker) {
                $manifest = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json -AsHashtable
                if ($manifest.ContainsKey('observed_build_bytes')) { $history = [Math]::Max($history, [long]$manifest.observed_build_bytes) }
            }
        }
    }
    # Cold-run fallback: 1 GiB native + 2 GiB shared .NET/npm restore/build + 50% native margin.
    $native = [Math]::Max(1GB, $history)
    $minimum = [long][Math]::Ceiling(($native * 1.5 + 2GB) / 1GB) * 1GB
    @{historical_native_bytes=$history;estimated_required_bytes=$minimum;warning_bytes=$minimum + 2GB}
}

function Start-AuraLocalCiStorage {
    param([string]$RepoRoot, [string]$BuildPath, [string]$RunId, [string]$OutputPath, [switch]$KeepBuild)
    $repo = Get-AuraFullPath $RepoRoot
    $ciRoot = Join-Path $repo 'build/ci'
    New-Item -ItemType Directory -Force -Path $ciRoot | Out-Null
    Assert-AuraNoReparse $ciRoot
    $canonicalLease = [IO.File]::Open((Join-Path $ciRoot '.canonical.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $runLease = $null; $evidenceLease = $null
    try {
        $estimate = Get-AuraCiStorageEstimate $repo
        $freeBeforePrune = Get-AuraFreeBytes $repo
        $logRoot = Join-Path $repo 'artifacts/ci/runs'
        $before = (Get-AuraTree $ciRoot).bytes + (Get-AuraTree $logRoot).bytes
        $prune = Invoke-AuraGeneratedCleanup -RepoRoot $repo -IncludeFailedRuns -Apply -Exclude @($BuildPath, $OutputPath)
        $prune | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputPath 'cleanup-start.json') -Encoding utf8
        $free = Get-AuraFreeBytes $repo
        if ($free -lt $estimate.estimated_required_bytes) {
            throw "Low disk: free $([Math]::Round($free/1GB,2)) GiB; estimated requirement $($estimate.estimated_required_bytes/1GB) GiB; reclaimable $([Math]::Round($prune.estimated_reclaimable_bytes/1GB,2)) GiB. Review: pwsh tools/dev/cleanup-generated.ps1 -DryRun -IncludeFailedRuns"
        }
        if ($free -lt $estimate.warning_bytes) { Write-Warning "Disk headroom: free $([Math]::Round($free/1GB,2)) GiB; estimated requirement $($estimate.estimated_required_bytes/1GB) GiB. Cleanup: pwsh tools/dev/cleanup-generated.ps1 -DryRun -IncludeFailedRuns" }
        if ((Test-Path -LiteralPath $BuildPath) -and -not $KeepBuild) { throw 'Fresh CI build directory already exists; refusing adoption' }
        New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null
        Assert-AuraNoReparse $BuildPath
        $runLease = [IO.File]::Open((Join-Path $BuildPath '.aura-active.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $manifest = [ordered]@{schema_version=1;owner='Aura.local-ci';repo=$repo;path=(Get-AuraFullPath $BuildPath);category='EPHEMERAL';
            run_id=$RunId;status='running';created_utc=[DateTime]::UtcNow.ToString('o');completed_utc=$null;pid=$PID;observed_build_bytes=0L}
        Write-AuraGeneratedManifest $BuildPath $manifest
        $evidenceManifest = $null
        # Only the new dedicated default run directory becomes retention-owned. Custom evidence is protected.
        if ((Split-Path $OutputPath -Parent) -eq (Join-Path $repo 'artifacts/ci/runs')) {
            Assert-AuraNoReparse $OutputPath
            $evidenceLease = [IO.File]::Open((Join-Path $OutputPath '.aura-active.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            $evidenceManifest = [ordered]@{}
            foreach ($key in $manifest.Keys) { $evidenceManifest[$key] = $manifest[$key] }
            $evidenceManifest.path = Get-AuraFullPath $OutputPath; $evidenceManifest.category = 'SHORT_RETENTION'
            Write-AuraGeneratedManifest $OutputPath $evidenceManifest
        }
        @{lease=$canonicalLease;run_lease=$runLease;evidence_lease=$evidenceLease;manifest=$manifest;evidence_manifest=$evidenceManifest;
            build=$BuildPath;output=$OutputPath;repo=$repo;keep_build=[bool]$KeepBuild;telemetry=[ordered]@{mode='local';generated_roots=@('build/ci','artifacts/ci/runs');free_bytes_before=$freeBeforePrune;free_bytes_after_start_prune=$free;generated_bytes_before=$before;
            historical_native_bytes=$estimate.historical_native_bytes;estimated_required_bytes=$estimate.estimated_required_bytes;
            approximate_build_bytes=0L;free_bytes_after=$null;generated_bytes_after=$null;reclaimed_logical_bytes=$prune.deleted_logical_bytes;retained_run_count=$prune.retained_run_count;retained_artifact_entries=$prune.retained_artifact_entries;
            cleanup_records=@('cleanup-start.json','cleanup-end.json');cleanup_error_count=@($prune.decisions | Where-Object action -eq 'error').Count;
            policy='Keep 3 per status/category; minimum age 24h; failed pruning opt-in; current run protected';measurement='Logical bytes at start/end, not a measured peak'}}
    } catch {
        if ($evidenceLease) { $evidenceLease.Dispose() }; if ($runLease) { $runLease.Dispose() }; $canonicalLease.Dispose(); throw
    }
}

function Complete-AuraLocalCiStorage {
    param([hashtable]$State, [string]$Outcome)
    try {
        $usage = (Get-AuraTree $State.build).bytes
        $State.telemetry.approximate_build_bytes = $usage
        if ($Outcome -eq 'passed' -and -not $State.keep_build) {
            # Completed, owned run only; preserve binaries, sources, logs and failure diagnostics.
            $tree = Get-AuraTree $State.build
            foreach ($file in $tree.files | Where-Object Extension -in @('.obj','.tlog','.pch','.ilk')) {
                Assert-AuraNoReparse $file.DirectoryName
                if ((Get-Item -LiteralPath $file.FullName -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse intermediate refused' }
                Remove-Item -LiteralPath $file.FullName -Force
                $State.telemetry.reclaimed_logical_bytes += $file.Length
            }
        }
        $State.manifest.observed_build_bytes = $usage; $State.manifest.status = $Outcome
        $State.manifest.completed_utc = [DateTime]::UtcNow.ToString('o'); $State.manifest.pid = 0
        Write-AuraGeneratedManifest $State.build $State.manifest
        if ($State.evidence_manifest) {
            $State.evidence_manifest.status = $Outcome; $State.evidence_manifest.completed_utc = $State.manifest.completed_utc; $State.evidence_manifest.pid = 0
            Write-AuraGeneratedManifest $State.output $State.evidence_manifest
        }
        $prune = Invoke-AuraGeneratedCleanup -RepoRoot $State.repo -IncludeFailedRuns -Apply -Exclude @($State.build, $State.output)
        $prune | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $State.output 'cleanup-end.json') -Encoding utf8
        $State.telemetry.cleanup_error_count += @($prune.decisions | Where-Object action -eq 'error').Count
        if ($State.telemetry.cleanup_error_count) { Write-Warning 'Some retention deletions were incomplete; inspect cleanup-start.json / cleanup-end.json before further cleanup.' }
        $State.telemetry.reclaimed_logical_bytes += $prune.deleted_logical_bytes
        $State.telemetry.retained_run_count = $prune.retained_run_count
        $State.telemetry.retained_artifact_entries = $prune.retained_artifact_entries
        $State.telemetry.generated_bytes_after = (Get-AuraTree (Join-Path $State.repo 'build/ci')).bytes + (Get-AuraTree (Join-Path $State.repo 'artifacts/ci/runs')).bytes
        $State.telemetry.free_bytes_after = Get-AuraFreeBytes $State.repo
    } finally {
        if ($State.evidence_lease) { $State.evidence_lease.Dispose() }; $State.run_lease.Dispose(); $State.lease.Dispose()
    }
}
