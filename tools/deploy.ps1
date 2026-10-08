# Put the hook DLL next to the working copy of the game (orig/pc/GameData, never the Steam install).
# Usage: pwsh tools/deploy.ps1 [-Config Release] [-Remove] [-KeepIni]
param([string]$Config = 'Release', [switch]$Remove, [switch]$KeepIni, [switch]$ResetIni)
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
$helper = Join-Path $root 'build/rthelper-x64/rt_helper.exe'  # 64-bit ray-tracing helper (src/rhi/vulkan/rt_ipc.h)
if (Test-Path $helper) { Copy-Item $helper (Join-Path $game 'rt_helper.exe') -Force }
$ffx = Join-Path $root 'build/rthelper-x64/amd_fidelityfx_vk.dll'  # AMD FSR 3.1 for the helper's AA (MIT, signed)
if (Test-Path $ffx) { Copy-Item $ffx (Join-Path $game 'amd_fidelityfx_vk.dll') -Force }
$ini = Join-Path $game 'and.ini'
# The player's and.ini is kept (test runs write their own); -ResetIni replaces it with the template.
if ($ResetIni -or -not (Test-Path $ini)) { Copy-Item (Join-Path $root 'hook/and.ini') $ini -Force }
Write-Host "deployed to $game"
