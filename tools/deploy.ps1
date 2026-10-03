# Put the hook DLL next to the working copy of the game (orig/pc/GameData, never the Steam install).
# Usage: pwsh tools/deploy.ps1 [-Config Release] [-Remove] [-KeepIni]
param([string]$Config = 'Release', [switch]$Remove, [switch]$KeepIni)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$game = Join-Path $root 'orig/pc/GameData'
$dll = Join-Path $game 'dinput8.dll'

if ($Remove) {
    if (Test-Path $dll) { Remove-Item -LiteralPath $dll -Confirm:$false }
    Write-Host 'hook removed: the game runs unmodified'
    return
}
$built = Join-Path $root "build/hook-$($Config.ToLower())/dinput8.dll"
# Refuse a stale build: if any source is newer than the DLL, the last build failed and tests would run old code.
$newest = Get-ChildItem (Join-Path $root 'src'), (Join-Path $root 'hook') -Recurse -File -Include *.cpp, *.h |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ((Get-Item $built).LastWriteTime -lt $newest.LastWriteTime) {
    throw "build is older than $($newest.Name): the last build failed; not deploying"
}
Copy-Item $built $dll -Force
$ini = Join-Path $game 'and.ini'
if (-not ($KeepIni -and (Test-Path $ini))) { Copy-Item (Join-Path $root 'hook/and.ini') $ini -Force }
Write-Host "deployed to $game"
