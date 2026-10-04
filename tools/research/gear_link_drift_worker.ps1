# Elevated research-only passive USB and realtime metadata observer.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$SessionDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$driftRoot = (Resolve-Path -LiteralPath $SessionDirectory).Path
$captureChild = $null
$traceStarted = $false
try {
    if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Passive attribution worker requires user-approved elevation'
    }
    $traceDll = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\Feedback\Microsoft.Diagnostics.Tracing.TraceEvent.dll'
    [Reflection.Assembly]::LoadFrom($traceDll) > $null
    $references = @(Get-ChildItem (Join-Path $PSHOME 'ref/*.dll') | ForEach-Object FullName) + @($traceDll)
    Add-Type -Path (Join-Path $PSScriptRoot 'gear_link_drift_trace.cs') -ReferencedAssemblies $references
    [GearDriftTrace]::Start((Join-Path $driftRoot 'trace'))
    $traceStarted = $true
    $captureScript = Join-Path $PSScriptRoot 'gear_link_capture_worker.ps1'
    $captureChild = Start-Process (Join-Path $PSHOME 'pwsh.exe') -ArgumentList @('-NoProfile','-File', $captureScript, '-SessionDirectory', $driftRoot) -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddMinutes(15)
    while (-not (Test-Path -LiteralPath (Join-Path $driftRoot 'state/stop-attribution')) -and [DateTime]::UtcNow -lt $deadline) {
        if ($captureChild.HasExited) { break }
        Start-Sleep -Milliseconds 250
    }
} catch {
    @{ state='failed'; error=$_.Exception.Message } | ConvertTo-Json | Set-Content (Join-Path $driftRoot 'trace/trace-status.json')
} finally {
    if ($traceStarted) { [GearDriftTrace]::Stop() }
    if ($null -ne $captureChild -and -not $captureChild.HasExited) {
        @{id="attribution-exit-$([DateTime]::UtcNow.Ticks)";action='exit'} | ConvertTo-Json | Set-Content (Join-Path $driftRoot 'state/capture-command.json')
        $captureChild.WaitForExit(10000) > $null
    }
}
