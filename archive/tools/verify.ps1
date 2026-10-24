# Build the hook DLL, deploy it (refuses stale builds) and run the comparison test. Stops at the first failure.
# Usage: pwsh archive/tools/verify.ps1 [-Only Name1,Name2] [-Scenario menu,level]
param([string]$Only = '', [string]$Scenario = 'all')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)  # the repository (this script lives in archive/tools)
& pwsh -NoProfile -File (Join-Path $root 'archive/tools/build.ps1') 2>&1 | Select-String ' error |warning C|built' | Out-Host
if ($LASTEXITCODE) { Write-Host 'BUILD FAILED'; exit 2 }
& pwsh -NoProfile -File (Join-Path $root 'archive/tools/deploy.ps1') -KeepIni | Out-Host
if ($LASTEXITCODE) { Write-Host 'DEPLOY REFUSED'; exit 2 }
$args = @((Join-Path $root 'tools/compare.py'), '--scenario', $Scenario)
if ($Only) { $args += @('--only', $Only) }
& (Join-Path $root '.venv/Scripts/python.exe') -u @args | Out-Host
exit $LASTEXITCODE
