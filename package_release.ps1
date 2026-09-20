# LEGACY C++ launcher packaging; does not package WinUI. See docs/development/PACKAGING.md.
<#
.SYNOPSIS
    Aura Release Packaging Script for PowerShell
    Usage in PowerShell:
        .\package_release.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host " ROG Falchion Ace HFX - Aura Standalone Release Packaging" -ForegroundColor Cyan
Write-Host "=========================================================" -ForegroundColor Cyan

$scriptPath = Join-Path $PSScriptRoot "tools\package_release.py"
if (-not (Test-Path $scriptPath)) {
    Write-Error "Packaging script not found: $scriptPath"
    exit 1
}

$pythonExe = $null
if (Get-Command python -ErrorAction SilentlyContinue) {
    $pythonExe = (Get-Command python).Source
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    $pythonExe = (Get-Command py).Source
} elseif (Test-Path "C:\Python313\python.exe") {
    $pythonExe = "C:\Python313\python.exe"
}

if (-not $pythonExe) {
    Write-Error "Python interpreter not found in PATH! Please install Python 3.8+."
    exit 1
}

Write-Host "[*] Using Python: $pythonExe" -ForegroundColor Gray
& $pythonExe $scriptPath

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[OK] Release build complete! Check the dist/ directory." -ForegroundColor Green
} else {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}
