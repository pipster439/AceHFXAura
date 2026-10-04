# Passive acquisition worker: never opens a HID interface or sends vendor commands.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$SessionDirectory,
      [string]$UsbPcapExecutable = 'F:\USBPcap\USBPcapCMD.exe')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sessionPath = (Resolve-Path -LiteralPath $SessionDirectory).Path
$commandPath = Join-Path $sessionPath 'state/capture-command.json'
$statusPath = Join-Path $sessionPath 'state/capture-worker.json'
$lastCommand = ''
$captureProcess = $null
$activePipe = $null
function Save-WorkerState([string]$State, [string]$Detail = '') {
    $tempPath = "$statusPath.tmp"
    [ordered]@{ schema_version=1; state=$State; detail=$Detail;
        capture_pipe=$activePipe; timestamp_utc=[DateTime]::UtcNow.ToString('o') } |
        ConvertTo-Json | Set-Content -LiteralPath $tempPath -Encoding utf8
    Move-Item -LiteralPath $tempPath -Destination $statusPath -Force
}
function Stop-OwnedCapture {
    if ($null -eq $captureProcess) { return }
    $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$($captureProcess.Id)"
    if ($null -ne $owned) {
        if ($owned.Name -ne 'USBPcapCMD.exe' -or
            $owned.ExecutablePath -ne $UsbPcapExecutable -or
            $owned.CommandLine -notlike "*$activePipe*") {
            throw 'Owned capture identity mismatch; refusing to stop any other process'
        }
        Stop-Process -Id $captureProcess.Id -ErrorAction Stop
    }
}
try {
    if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'USBPcap capture worker requires user-approved elevation'
    }
    Save-WorkerState 'ready'
    $idleDeadline = [DateTime]::UtcNow.AddMinutes(10)
    while ($true) {
        if (-not (Test-Path -LiteralPath $commandPath)) {
            if ($null -eq $captureProcess -and [DateTime]::UtcNow -gt $idleDeadline) { break }
            Start-Sleep -Milliseconds 150; continue
        }
        $command = Get-Content -LiteralPath $commandPath -Raw | ConvertFrom-Json
        if ([string]$command.id -eq $lastCommand) {
            if ($null -eq $captureProcess -and [DateTime]::UtcNow -gt $idleDeadline) { break }
            Start-Sleep -Milliseconds 150; continue
        }
        $lastCommand = [string]$command.id
        switch ([string]$command.action) {
            'start' {
                if ($null -ne $captureProcess) { throw 'Capture already owned; stop it first' }
                if ([string]$command.case_name -notmatch '^[a-z0-9_]{1,60}$') { throw 'Invalid capture case name' }
                if ([string]$command.pipe_name -notmatch '^aura_audit_[a-z0-9_]{1,80}$') { throw 'Invalid pipe name' }
                $others = @(Get-CimInstance Win32_Process -Filter "Name='USBPcapCMD.exe'")
                if ($others.Count -ne 0) { throw 'Existing USBPcap instance detected; will not start another' }
                $activePipe = "\\.\pipe\$($command.pipe_name)"
                $outLog = Join-Path $sessionPath "logs/$($command.case_name)-capture.stdout.log"
                $errLog = Join-Path $sessionPath "logs/$($command.case_name)-capture.stderr.log"
                # Only fixed controller USBPcap4 and a validated pipe are allowed.
                # Installed 1.5.4 usage permits a 4096-byte capture buffer.
                # Reduce passive evidence delivery latency, never HID timing.
                $captureProcess = Start-Process -FilePath $UsbPcapExecutable -ArgumentList @('-d','\\.\USBPcap4','-b','4096','-A','--inject-descriptors','-o',$activePipe) -WindowStyle Hidden -PassThru -RedirectStandardOutput $outLog -RedirectStandardError $errLog
                Save-WorkerState 'capturing' ([string]$command.case_name)
            }
            'stop' {
                Stop-OwnedCapture
                $captureProcess = $null; $activePipe = $null
                $idleDeadline = [DateTime]::UtcNow.AddMinutes(10)
                Save-WorkerState 'ready' 'Owned passive capture stopped; pipe receiver flushes retained records'
            }
            'exit' { Stop-OwnedCapture; $captureProcess=$null; Save-WorkerState 'exited'; return }
            default { throw 'Unsupported command; no process launched' }
        }
    }
    Save-WorkerState 'exited' 'Idle timeout'
} catch {
    try { Stop-OwnedCapture } catch { }
    Save-WorkerState 'failed' $_.Exception.Message
    exit 1
} finally {
    Stop-OwnedCapture
}
