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
Copy-Item (Join-Path $root "build/hook-$($Config.ToLower())/dinput8.dll") $dll -Force
$ini = Join-Path $game 'and.ini'
if (-not ($KeepIni -and (Test-Path $ini))) { Copy-Item (Join-Path $root 'hook/and.ini') $ini -Force }
Write-Host "deployed to $game"
