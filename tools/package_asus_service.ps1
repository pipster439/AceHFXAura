param([string]$OutputDirectory = "audit_artifacts/phase3-m1/service-candidate")
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidatePath = [IO.Path]::GetFullPath($OutputDirectory, $repoRoot)
if (Test-Path -LiteralPath $candidatePath) { throw 'Candidate directory already exists. Choose a fresh directory.' }
& dotnet publish (Join-Path $repoRoot 'src/AceHFXService/AceHFXService.csproj') -c Release -r win-x64 --self-contained true -o $candidatePath
if ($LASTEXITCODE -ne 0) { throw 'Service publish failed.' }
$files = Get-ChildItem -LiteralPath $candidatePath -File | Sort-Object Name
$manifest = [ordered]@{ schemaVersion = 1; runtime = 'win-x64'; service = 'AceHFXService'; files = @($files | ForEach-Object {
    [ordered]@{ name = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
}) }
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $candidatePath 'candidate-manifest.json') -Encoding utf8
Write-Output $candidatePath
