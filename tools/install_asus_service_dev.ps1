param([Parameter(Mandatory = $true)][string]$CandidateDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$admin = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run this reviewed installer from an elevated PowerShell.' }
if (Get-Service -Name AceHFXService -ErrorAction SilentlyContinue) { throw 'AceHFXService already exists; no existing service is replaced or stopped.' }
$sourcePath = (Resolve-Path -LiteralPath $CandidateDirectory).Path
$destinationPath = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'AceHFXAura/AceHFXService-M1'
if (Test-Path -LiteralPath $destinationPath) { throw 'Installation directory already exists; use explicit manual cleanup before a new install.' }
if ((Get-Item -LiteralPath $sourcePath).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse source rejected.' }
if (@(Get-ChildItem -LiteralPath $sourcePath -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Reparse payload rejected.' }
$manifestPath = Join-Path $sourcePath 'candidate-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or $manifest.service -ne 'AceHFXService' -or $manifest.runtime -ne 'win-x64') { throw 'Invalid candidate manifest.' }
if ('AceHFXService.exe' -notin $manifest.files.name -or 'coreclr.dll' -notin $manifest.files.name) { throw 'Incomplete self-contained candidate.' }
if (@(Get-ChildItem -LiteralPath $sourcePath -Directory).Count) { throw 'Unexpected payload directories.' }
if (@(Get-ChildItem -LiteralPath $sourcePath -File).Count -ne $manifest.files.Count + 1) { throw 'Unlisted candidate files.' }
foreach ($entry in $manifest.files) {
    if ([IO.Path]::GetFileName($entry.name) -ne $entry.name -or $entry.name -match '[:\\/]') { throw 'Invalid payload name.' }
    if ((Get-FileHash -LiteralPath (Join-Path $sourcePath $entry.name) -Algorithm SHA256).Hash -ne $entry.sha256) { throw 'Candidate hash mismatch.' }
}
# Create the protected destination before copying, and verify again there before SCM
# registration. Never execute a service binary/dependency from the writable checkout.
New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
& icacls.exe $destinationPath /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)(F)' '*S-1-5-32-544:(OI)(CI)(F)' '*S-1-5-32-545:(OI)(CI)(RX)'
if ($LASTEXITCODE -ne 0) { throw 'Failed to protect service directory.' }
foreach ($entry in $manifest.files) {
    $installedPath = Join-Path $destinationPath $entry.name
    Copy-Item -LiteralPath (Join-Path $sourcePath $entry.name) -Destination $installedPath
    if ((Get-FileHash -LiteralPath $installedPath -Algorithm SHA256).Hash -ne $entry.sha256) { throw 'Installed hash mismatch.' }
}
if (-not [Diagnostics.EventLog]::SourceExists('AceHFXService')) { [Diagnostics.EventLog]::CreateEventSource('AceHFXService', 'Application') }
New-Service -Name AceHFXService -DisplayName 'AceHFXAura platform broker (M1)' -BinaryPathName ('"' + (Join-Path $destinationPath 'AceHFXService.exe') + '"') -StartupType Manual -Description 'Read-only ASUS platform capability broker. No hardware control in M1.' | Out-Null
Start-Service -Name AceHFXService
Get-Service -Name AceHFXService | Format-List Name, Status, StartType
