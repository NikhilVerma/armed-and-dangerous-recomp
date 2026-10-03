# Build the hook DLL, deploy it (refuses stale builds) and run the comparison test. Stops at the first failure.
# Usage: pwsh tools/verify.ps1 [-Only Name1,Name2] [-Scenario menu,level]
param([string]$Only = '', [string]$Scenario = 'all')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& pwsh -NoProfile -File (Join-Path $root 'tools/build.ps1') 2>&1 | Select-String ' error |warning C|built' | Out-Host
if ($LASTEXITCODE) { Write-Host 'BUILD FAILED'; exit 2 }
& pwsh -NoProfile -File (Join-Path $root 'tools/deploy.ps1') -KeepIni | Out-Host
if ($LASTEXITCODE) { Write-Host 'DEPLOY REFUSED'; exit 2 }
$args = @((Join-Path $root 'tools/compare.py'), '--scenario', $Scenario)
if ($Only) { $args += @('--only', $Only) }
& (Join-Path $root '.venv/Scripts/python.exe') -u @args | Out-Host
exit $LASTEXITCODE
