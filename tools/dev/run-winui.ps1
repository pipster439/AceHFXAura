#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('x64')][string]$Platform = 'x64',
    [switch]$NoBuild,
    [switch]$NoLaunch,
    [switch]$Restart,
    [switch]$IsolatedData
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$build = Join-Path $repo 'build/dev-winui'
$statePath = Join-Path $build 'launch.json'
$clock = [Diagnostics.Stopwatch]::StartNew()
function Invoke-Checked([string]$Tool, [string[]]$Arguments) {
    Write-Verbose "$Tool $($Arguments -join ' ')"
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Tool failed (exit $LASTEXITCODE)." }
}
function Get-DevRuntime {
    try { Invoke-RestMethod 'http://127.0.0.1:19897/api/runtime/status' -TimeoutSec 2 }
    catch { return $null }
}
if (-not $IsWindows) { throw 'WinUI development launch requires Windows.' }
if (-not $NoLaunch -and ($env:CI -eq 'true' -or $env:GITHUB_ACTIONS -eq 'true')) {
    throw 'Development app launch is local-only; CI must use -NoLaunch.'
}
if (-not (Test-Path (Join-Path $repo 'winui/Aura.WinUI.csproj'))) { throw 'Repository root not found.' }
foreach ($tool in @('dotnet', 'cmake')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool missing: $tool" }
}
# The AppInstance key and loopback port are shared with installed Aura. Never attach silently.
$running = @(Get-Process Aura -ErrorAction SilentlyContinue)
$daemons = @(Get-Process aura_daemon -ErrorAction SilentlyContinue)
$listeners = @(Get-NetTCPConnection -LocalPort 19897 -State Listen -ErrorAction SilentlyContinue)
if ($running.Count -or $daemons.Count -or $listeners.Count) {
    $owned = $null
    if (Test-Path $statePath) { $owned = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json }
    $matches = $null -ne $owned -and $running.Count -eq 1 -and
        $running[0].Id -eq $owned.ui_pid -and $running[0].Path -eq $owned.executable -and
        $running[0].StartTime.ToUniversalTime() -eq ([DateTimeOffset]$owned.ui_started_at_utc).UtcDateTime
    if ($matches) {
        foreach ($daemon in $daemons) {
            $info = Get-CimInstance Win32_Process -Filter "ProcessId=$($daemon.Id)"
            if ($info.ParentProcessId -ne $owned.ui_pid -or $info.ExecutablePath -ne $owned.daemon_binary) { $matches = $false }
        }
        foreach ($listener in $listeners) {
            if ($listener.OwningProcess -notin @($daemons.Id)) { $matches = $false }
        }
        $status = Get-DevRuntime
        if ($listeners.Count -and ($null -eq $status -or
            $status.identity.instance_id -ne $owned.daemon_instance_id)) { $matches = $false }
    }
    if (-not $matches) { throw 'Existing external Aura/daemon detected (or ownership cannot be verified). Exit it normally before development launch; no process was stopped.' }
    if (-not $Restart) { throw 'This development app is already running. Exit normally or use -Restart.' }
    # WM_CLOSE follows existing app shutdown. Minimize-to-tray may intentionally refuse exit.
    if (-not $running[0].CloseMainWindow() -or -not $running[0].WaitForExit(10000)) {
        throw 'Normal window close did not exit Aura (possibly minimize-to-tray). Use the Aura tray Exit command; no force termination was attempted.'
    }
    if (@(Get-Process aura_daemon -ErrorAction SilentlyContinue).Count -or
        @(Get-NetTCPConnection -LocalPort 19897 -State Listen -ErrorAction SilentlyContinue).Count) {
        throw 'Daemon remains after normal UI exit. Exit it normally; no external process will be stopped.'
    }
}
New-Item -ItemType Directory -Force -Path $build | Out-Null
Push-Location $repo
try {
    if (-not $NoBuild) {
        Invoke-Checked cmake @('-S', $repo, '-B', $build, '-A', $Platform)
        Invoke-Checked cmake @('--build', $build, '--config', $Configuration, '--target', 'aura_daemon', 'aura_web_ui', '--parallel')
        Invoke-Checked dotnet @('build', 'winui/Aura.WinUI.csproj', '-c', $Configuration, "-p:Platform=$Platform", '--nologo')
    }
    # Query evaluated MSBuild properties: do not hardcode the SDK/TFM or executable name.
    $properties = & dotnet msbuild winui/Aura.WinUI.csproj "-p:Configuration=$Configuration" "-p:Platform=$Platform" '-getProperty:TargetDir,AssemblyName,WindowsPackageType'
    if ($LASTEXITCODE -ne 0) { throw 'Could not resolve WinUI build output.' }
    $p = ($properties -join "`n" | ConvertFrom-Json).Properties
    if ($p.WindowsPackageType -ne 'None') { throw 'Project packaging model changed. Review development deployment before launching.' }
    $exe = Join-Path $p.TargetDir ($p.AssemblyName + '.exe')
    $bin = Join-Path $build $Configuration
    $daemon = Join-Path $bin 'aura_daemon.exe'
    foreach ($file in @($exe, $daemon, (Join-Path $bin 'aura_web_ui.exe'))) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Build output missing: $file. Run without -NoBuild." }
    }
    $data = if ($IsolatedData) { Join-Path $build 'data' } else {
        if ($env:AURA_DATA_ROOT) { [IO.Path]::GetFullPath($env:AURA_DATA_ROOT) } else { Join-Path $env:LOCALAPPDATA 'Aura' }
    }
    $result = [ordered]@{ schema_version = 1; configuration = $Configuration; platform = $Platform
        executable = $exe; package_identity = $null; deployment = 'unpackaged-self-contained'
        daemon_binary = $daemon; data_root = $data; health_endpoint = 'http://127.0.0.1:19897/api/runtime/status'
        ui_pid = $null; ui_started_at_utc = $null; daemon_instance_id = $null
        product_version = $null; duration_ms = 0; launched = $false }
    if (-not $NoLaunch) {
        $saved = @{}
        $environment = @{ AURA_DEV_ROOT = $repo; AURA_DEV_BIN = $bin; AURA_DATA_ROOT = $data }
        try {
            foreach ($key in $environment.Keys) {
                $saved[$key] = [Environment]::GetEnvironmentVariable($key)
                [Environment]::SetEnvironmentVariable($key, $environment[$key])
            }
            $ui = Start-Process -FilePath $exe -WorkingDirectory $repo -PassThru
        } finally {
            foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key]) }
        }
        $result.ui_pid = $ui.Id
        $result.ui_started_at_utc = $ui.StartTime.ToUniversalTime().ToString('o')
        # Retain ownership even if startup fails. Read-only health check, never HID probing.
        $result | ConvertTo-Json | Set-Content -LiteralPath $statePath
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        $ready = $false
        while ([DateTime]::UtcNow -lt $deadline -and -not $ui.HasExited) {
            $status = Get-DevRuntime
            if ($null -ne $status -and $status.status -eq 'ok') {
                $identity = $status.identity
                $child = Get-CimInstance Win32_Process -Filter "ProcessId=$($identity.process_id)"
                if ($null -eq $child -or $child.ParentProcessId -ne $ui.Id -or $child.ExecutablePath -ne $daemon) {
                    throw 'Health endpoint belongs to an unexpected daemon. Exit Aura normally; this tool will not stop an external service.'
                }
                $result.daemon_instance_id = $identity.instance_id
                $result.product_version = $identity.product_version
                $ready = $true
                break
            }
            Start-Sleep -Milliseconds 200
        }
        $result.launched = $ready
        $result.duration_ms = $clock.ElapsedMilliseconds
        $result | ConvertTo-Json | Set-Content -LiteralPath $statePath
        if (-not $ready) { throw "Development daemon did not become ready. Inspect $data/aura_daemon.log and exit Aura normally." }
    }
    $result.duration_ms = $clock.ElapsedMilliseconds
    $result | ConvertTo-Json
} finally { Pop-Location }
