# Clone the pinned vendor anti-aliasing SDKs into work/sdk (not committed; archive/rthelper/CMakeLists.txt finds them there).
# The human accepted both licences on 2026-10-08 (docs/decision.md): NVIDIA RTX SDKs licence (DLSS), Intel Simplified
# Software Licence (XeSS).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)  # the repository (this script lives in archive/tools)
$sdk = Join-Path $root 'work/sdk'
New-Item -ItemType Directory -Force $sdk | Out-Null
foreach ($s in @(@{ Name = 'dlss'; Url = 'https://github.com/NVIDIA/DLSS.git'; Tag = 'v310.9.1' },
                 @{ Name = 'xess'; Url = 'https://github.com/intel/xess.git'; Tag = 'v3.0.2' })) {
    $dir = Join-Path $sdk $s.Name
    if (Test-Path $dir) { Write-Host "$($s.Name): present"; continue }
    git -c advice.detachedHead=false clone -q --depth 1 --branch $s.Tag $s.Url $dir
    if ($LASTEXITCODE) { throw "clone of $($s.Url) failed" }
    Write-Host "$($s.Name): $($s.Tag)"
}
