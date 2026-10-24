# Build the release zips from compiled outputs only: never source, symbols, docs or game files (the release plan:
# a binary-only mod; players bring their own copy of the game). Writes work/release/<version>/:
#   AnD-Remaster-<version>.zip          dinput8.dll (the hook / injector), rt_helper.exe, amd_fidelityfx_vk.dll,
#                                       and.ini, README.txt, THIRD_PARTY_NOTICES.txt
#   AnD-Remaster-<version>-DLSS-XeSS.zip  optional: nvngx_dlss.dll, libxess.dll and their licence texts (the game runs
#                                       FSR in their place when they are missing)
# Usage: pwsh archive/tools/package_release.ps1 -Version 0.1.0
param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)  # the repository (this script lives in archive/tools)
$hook = Join-Path $root 'build/hook-release'
$helper = Join-Path $root 'build/rthelper-x64'
$sdk = Join-Path $root 'work/sdk'
$out = Join-Path $root "work/release/$Version"
if (Test-Path $out) { Remove-Item -Recurse -Force $out -Confirm:$false }
$core = Join-Path $out 'core'
$pack = Join-Path $out 'dlss-xess'
New-Item -ItemType Directory -Force $core, $pack | Out-Null

function Need($path) { if (-not (Test-Path $path)) { throw "missing $path (build first)" }; $path }

# Core: the compiled mod.
Copy-Item (Need (Join-Path $hook 'dinput8.dll')) $core
Copy-Item (Need (Join-Path $helper 'rt_helper.exe')) $core
Copy-Item (Need (Join-Path $helper 'amd_fidelityfx_vk.dll')) $core
Copy-Item (Need (Join-Path $root 'archive/hook/and.ini')) $core

# Third-party notices: every library compiled into or shipped with the mod.
$notices = @(
    @{ Name = 'AMD FidelityFX SDK (FSR 3.1, Shadow Denoiser)'; File = "$hook/_deps/ffx_sdk-src/LICENSE.txt" },
    @{ Name = 'XeGTAO (Intel, MIT)'; File = "$root/archive/third_party/XeGTAO/LICENSE" },
    @{ Name = 'MinHook'; File = "$hook/_deps/minhook-src/LICENSE.txt" },
    @{ Name = 'volk'; File = "$hook/_deps/volk-src/LICENSE.md" },
    @{ Name = 'Vulkan-Headers'; File = "$hook/_deps/vulkan_headers-src/LICENSE.md" },
    @{ Name = 'glslang'; File = "$hook/_deps/glslang-src/LICENSE.txt" },
    @{ Name = 'Jolt Physics'; File = "$hook/_deps/jolt-src/LICENSE" },
    @{ Name = 'NVIDIA DLSS SDK (NGX, linked into rt_helper.exe)'; File = "$sdk/dlss/LICENSE.txt" }
)
function WriteNotices($path, $list, $intro) {
    $text = $intro + "`r`n"
    foreach ($n in $list) {
        $f = Get-ChildItem -Path (Split-Path $n.File) -Filter (Split-Path -Leaf $n.File) -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if (-not $f) { throw "licence text missing for $($n.Name): $($n.File)" }
        $text += "`r`n==== $($n.Name) ====`r`n`r`n" + (Get-Content -Raw $f.FullName) + "`r`n"
    }
    Set-Content -Path $path -Value $text -Encoding utf8
}
WriteNotices (Join-Path $core 'THIRD_PARTY_NOTICES.txt') $notices `
    'Armed and Dangerous Remaster uses the following third-party software. This software contains NVIDIA DLSS.'

@"
Armed and Dangerous Remaster $Version (unofficial fan mod)

Requires your own copy of Armed and Dangerous (PC). No game files are included.

Install: copy every file in this zip into the game's GameData folder (next to game.exe). Remove dinput8.dll to
return to the unmodified game. Settings: and.ini, or Options > Remaster in game.

Anti-aliasing: AMD FSR 3.1 is included. NVIDIA DLSS (DLAA) and Intel XeSS need the optional
"DLSS-XeSS" pack; without it those modes use FSR.

Credits: AMD FidelityFX FSR 3.1, NVIDIA DLSS, Intel XeSS. Third-party licences: THIRD_PARTY_NOTICES.txt.
"@ | Set-Content -Path (Join-Path $core 'README.txt') -Encoding utf8

# Optional vendor pack.
Copy-Item (Need (Join-Path $helper 'nvngx_dlss.dll')) $pack
Copy-Item (Need (Join-Path $helper 'libxess.dll')) $pack
WriteNotices (Join-Path $pack 'THIRD_PARTY_NOTICES.txt') @(
    @{ Name = 'NVIDIA DLSS (nvngx_dlss.dll)'; File = "$sdk/dlss/LICENSE.txt" },
    @{ Name = 'Intel XeSS (libxess.dll)'; File = "$sdk/xess/LICENSE.txt" }
) 'Optional anti-aliasing libraries for Armed and Dangerous Remaster. This software contains NVIDIA DLSS and Intel XeSS.'
@"
Optional DLSS / XeSS pack for Armed and Dangerous Remaster $Version.
Copy both DLLs into the game's GameData folder, next to rt_helper.exe. Then pick "DLSS / DLAA" (NVIDIA RTX) or
"XeSS" in Options > Remaster > Anti-aliasing, or set [render] aa=2 / aa=3 in and.ini.
"@ | Set-Content -Path (Join-Path $pack 'README.txt') -Encoding utf8

Compress-Archive -Path (Join-Path $core '*') -DestinationPath (Join-Path $out "AnD-Remaster-$Version.zip")
Compress-Archive -Path (Join-Path $pack '*') -DestinationPath (Join-Path $out "AnD-Remaster-$Version-DLSS-XeSS.zip")
Get-ChildItem $out -Filter *.zip | ForEach-Object { '{0}  {1:N1} MB' -f $_.Name, ($_.Length / 1MB) }
