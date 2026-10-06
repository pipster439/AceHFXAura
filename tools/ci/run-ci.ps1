#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$SkipFrontend,
    [switch]$SkipWinUI,
    [switch]$KeepBuild,
    [string]$OutputDirectory = 'artifacts/ci'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
. "$PSScriptRoot/ci-common.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory, $repo)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$summary = [ordered]@{
    schema_version = 1; configuration = $Configuration; working_tree_clean = $false
    started_at_utc = [DateTime]::UtcNow.ToString('o'); completed_at_utc = $null
    complete_required_run = -not ($SkipFrontend -or $SkipWinUI)
    stages = [Collections.Generic.List[object]]::new(); overall = 'running'
}
$savedEnv = @{}
$environment = @{
    CI = 'true'; PYTHONUTF8 = '1'; AURA_MAGNETIC_VALIDATION_OFFLINE = '1'
    AURA_PACKAGE_DIR = $null; AURA_UI_EXE = $null; AURA_UI_VALIDATION_DIR = $null
    AURA_STUDIO_EXE = $null; AURA_STUDIO_VALIDATION_DIR = $null
}
$runId = if ($KeepBuild) { 'kept' } else { [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8) }
$build = Join-Path $repo "build/ci/$runId"
$testResults = Join-Path $output ("dotnet-test/" + [Guid]::NewGuid().ToString('N'))
$summary.test_results_directory = [IO.Path]::GetRelativePath($output, $testResults)
$environment.AURA_BIN_DIR = Join-Path $build $Configuration
$environment.AURA_INTEGRATION_BIN = $environment.AURA_BIN_DIR
Push-Location $repo
$success = $false
try {
    # Stamp all known logs upfront; an early failure must not present old build logs as fresh evidence.
    $planned = [ordered]@{
        'guards'='guards.log'; 'diff-check'='diff-check.log'; 'frontend-dependencies'='frontend-install.log'
        'configure'='configure.log'; 'native-build'='native-build.log'; 'ctest-discovery'='ctest-discovery.log'
        'ctest'='ctest.log'; 'daemon-integration'='daemon-integration.log'; 'dotnet-test'='dotnet-test.log'
        'asus-platform-test'='asus-platform-test.log'
        'fan-typelib-validator-test'='fan-typelib-validator-test.log'
        'aura-gate-a-software'='aura-gate-a-software.log'
        'aura-enumeration-software'='aura-enumeration-software.log'
        'aura-mta-triage-software'='aura-mta-triage-software.log'
        'winui-build'='winui-build.log'; 'frontend-test'='frontend-test.log'; 'frontend-build'='frontend-build.log'
    }
    foreach ($name in $planned.Keys) {
        $summary.stages.Add([ordered]@{name=$name;outcome='not-run';duration_ms=0;exit_code=$null;log=$planned[$name]})
        "Not executed in run starting $($summary.started_at_utc)" | Set-Content -LiteralPath (Join-Path $output $planned[$name])
    }
    foreach ($key in $environment.Keys) {
        $savedEnv[$key] = [Environment]::GetEnvironmentVariable($key)
        [Environment]::SetEnvironmentVariable($key, $environment[$key])
    }
    $summary.working_tree_clean = (@(& git status --porcelain).Count -eq 0)
    Invoke-CiStage $summary $output 'guards' 'guards.log' {
        if (-not $IsWindows) { throw 'Windows/MSVC software CI requires PowerShell 7 on Windows.' }
        foreach ($tool in @('git', 'python', 'node', 'npm', 'cmake', 'ctest', 'dotnet')) {
            if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool missing: $tool" }
        }
        Invoke-CiCommand python @('-B', 'tools/ci/check-source.py')
        Invoke-CiCommand python @('-B', 'tools/ci/test_fan_read_only.py')
        Invoke-CiCommand python @('-B', 'tools/ci/check-idle-test-ports.py')
    }
    Invoke-CiStage $summary $output 'diff-check' 'diff-check.log' { Invoke-CiCommand git @('diff', '--check') }
    Invoke-CiStage $summary $output 'aura-gate-a-software' 'aura-gate-a-software.log' {
        $gateBuild = Join-Path $build 'aura-gate-a-software'
        Invoke-CiCommand python @('-B', '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_aura_gate_a_guards.py', '-v')
        Invoke-CiCommand cmake @('-S', 'tools/AuraOwnershipExperiment', '-B', $gateBuild, '-A', 'x64', '-DAURA_GATE_A_SOFTWARE_TESTS=ON')
        # CI compiles/runs only the fake API tests. Never build/run the actual candidate here.
        Invoke-CiCommand cmake @('--build', $gateBuild, '--config', $Configuration, '--target', 'AuraGateATests', 'AuraGateAOwnerFixture')
        Invoke-CiCommand ctest @('--test-dir', $gateBuild, '-C', $Configuration, '--output-on-failure', '--no-tests=error',
            '--output-junit', (Join-Path $output 'aura-gate-a-tests.xml'))
    }
    Invoke-CiStage $summary $output 'aura-enumeration-software' 'aura-enumeration-software.log' {
        Invoke-CiCommand python @('-B', '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_aura_enumeration_characterizer.py', '-v')
        $characterizerBuild = Join-Path $build 'aura-enumeration-characterizer'
        Invoke-CiCommand cmake @('-S', 'tools/AuraEnumerationCharacterizer', '-B', $characterizerBuild, '-A', 'x64')
        # Build/validate the reduced ABI; do not execute COM discovery in software CI.
        Invoke-CiCommand cmake @('--build', $characterizerBuild, '--config', $Configuration, '--target', 'AuraEnumerationCharacterizer')
    }
    Invoke-CiStage $summary $output 'aura-mta-triage-software' 'aura-mta-triage-software.log' {
        Invoke-CiCommand python @('-B', '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_aura_mta_triage.py', '-v')
        $probeBuild = Join-Path $build 'mta-probe'
        $debugBuild = Join-Path $build 'mta-debugger'
        Invoke-CiCommand cmake @('-S', 'tools/AuraMtaCrashProbe', '-B', $probeBuild, '-A', 'x64')
        Invoke-CiCommand cmake @('--build', $probeBuild, '--config', $Configuration)
        Invoke-CiCommand cmake @('-S', 'tools/AuraMtaDebugLauncher', '-B', $debugBuild, '-A', 'x64')
        Invoke-CiCommand cmake @('--build', $debugBuild, '--config', $Configuration)
        # Software CI never starts a vendor COM trial.
    }
    Invoke-CiStage $summary $output 'lighting-backend-software' 'lighting-backend-software.log' {
        Invoke-CiCommand python @('-B', '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_lighting_backend_probe.py', '-v')
        Invoke-CiCommand python @('-B', '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_servicemediator_contract.py', '-v')
        $lightingProbeBuild = Join-Path $build 'lighting-backend-probe'
        Invoke-CiCommand cmake @('-S', 'tools/LightingBackendProbe', '-B', $lightingProbeBuild, '-A', 'x64')
        # Compile only. Actual mediator/WDL/MMF observations are opt-in local research.
        Invoke-CiCommand cmake @('--build', $lightingProbeBuild, '--config', $Configuration)
    }
    # Required before configure, even with SkipFrontend: CMake registers real Studio fixture DLL tests.
    Invoke-CiStage $summary $output 'frontend-dependencies' 'frontend-install.log' {
        Push-Location frontend
        try { Invoke-CiCommand npm @('ci') } finally { Pop-Location }
    }
    Invoke-CiStage $summary $output 'configure' 'configure.log' {
        # Let installed CMake select its supported Visual Studio generator.
        # No install/upgrade, no existing build-directory dependency (VS 2022/2026 hosts).
        Invoke-CiCommand cmake @('-S', '.', '-B', $build, '-A', 'x64')
    }
    Invoke-CiStage $summary $output 'native-build' 'native-build.log' {
        Invoke-CiCommand cmake @('--build', $build, '--config', $Configuration, '--parallel', '2')
    }
    Invoke-CiStage $summary $output 'ctest-discovery' 'ctest-discovery.log' {
        $discovery = & ctest --test-dir $build -C $Configuration --show-only=json-v1
        if ($LASTEXITCODE -ne 0) { throw 'CTest discovery failed' }
        $discovery | Set-Content -LiteralPath (Join-Path $output 'ctest-discovery.json') -Encoding utf8
        $tests = ($discovery -join "`n" | ConvertFrom-Json).tests
        if ($tests.Count -eq 0) { throw 'CTest has no registered tests' }
        # Dependencies cannot silently remove permanent suites from a fresh configure.
        foreach ($name in @('device_profile_runtime', 'device_profile_automation_coordinator',
            'device_profile_binding_engine', 'automation_reconciliation', 'automation_migration',
            'stage0_fixture_integrity', 'ci_infrastructure', 'native_hardware_ci_guard')) {
            if ($name -notin $tests.name) { throw "Required permanent suite not registered: $name" }
        }
        "Discovered $($tests.Count) tests; executing the entire registry."
    }
    Invoke-CiStage $summary $output 'ctest' 'ctest.log' {
        Invoke-CiCommand ctest @('--test-dir', $build, '-C', $Configuration, '--output-on-failure',
            '--no-tests=error', '--output-junit', (Join-Path $output 'ctest-results.xml'))
    }
    Invoke-CiStage $summary $output 'daemon-integration' 'daemon-integration.log' {
        Push-Location tests
        try {
            Invoke-CiCommand python @('-B', '-m', 'unittest', '-v',
                'test_runtime_entrypoints.TestWebUiEntrypoint', 'test_runtime_entrypoints.TestDaemonEntrypoint',
                'test_automation_v2_daemon', 'test_automation_authoring_daemon', 'test_automation_effect_daemon',
                'test_automation_reload_daemon', 'test_automation_retrigger_daemon', 'test_global_lighting_daemon',
                'test_winui_productization.TestGsiConfigurationContract', 'test_gsi_dictionary_blocks',
                'test_release_archive_guards', 'test_packaged_studio_paths')
        } finally { Pop-Location }
    }
    Invoke-CiStage $summary $output 'dotnet-test' 'dotnet-test.log' {
        Invoke-CiCommand dotnet @('test', 'tests/Aura.Tests/Aura.Tests.csproj', '-c', $Configuration,
            '--nologo', '--filter', 'TestCategory!=DesktopSmoke', '--logger', 'trx;LogFileName=aura.trx',
            '--results-directory', $testResults)
    }
    Invoke-CiStage $summary $output 'winui-build' 'winui-build.log' -Skip:$SkipWinUI -Action {
        Invoke-CiCommand dotnet @('build', 'winui/Aura.WinUI.csproj', '-c', $Configuration, '-p:Platform=x64', '--nologo')
    }
    Invoke-CiStage $summary $output 'asus-platform-test' 'asus-platform-test.log' {
        Invoke-CiCommand dotnet @('test', 'tests/AsusPlatform.Tests/AsusPlatform.Tests.csproj', '-c', $Configuration,
            '--nologo', '--filter', 'TestCategory!=DesktopSmoke', '--logger', 'trx;LogFileName=asus-platform.trx',
            '--results-directory', $testResults)
    }
    Invoke-CiStage $summary $output 'fan-typelib-validator-test' 'fan-typelib-validator-test.log' {
        Invoke-CiCommand dotnet @('test', 'tests/FanTypeLibValidator.Tests/FanTypeLibValidator.Tests.csproj', '-c', $Configuration,
            '--nologo', '--filter', 'TestCategory!=OfflineMetadata', '--logger', 'trx;LogFileName=fan-typelib-validator.trx',
            '--results-directory', $testResults)
    }
    Invoke-CiStage $summary $output 'frontend-test' 'frontend-test.log' -Skip:$SkipFrontend -Action {
        Push-Location frontend
        try { Invoke-CiCommand npm @('test') } finally { Pop-Location }
    }
    Invoke-CiStage $summary $output 'frontend-build' 'frontend-build.log' -Skip:$SkipFrontend -Action {
        Push-Location frontend
        # Vite defaults to tracked web/index.html. Verify build without overwriting the user's page.
        try { Invoke-CiCommand npm @('run', 'build', '--', '--outDir', (Join-Path $build 'frontend')) }
        finally { Pop-Location }
    }
    $summary.overall = if ($summary.complete_required_run) { 'passed' } else { 'partial' }
    $success = $true
} catch {
    $summary.overall = 'failed'
    Write-Host "CI failed: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    $summary.completed_at_utc = [DateTime]::UtcNow.ToString('o')
    Write-CiSummary $summary $output
    foreach ($key in $savedEnv.Keys) { [Environment]::SetEnvironmentVariable($key, $savedEnv[$key]) }
    Pop-Location
}
if (-not $success) { exit 1 }
