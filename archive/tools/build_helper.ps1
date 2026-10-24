# Build the 64-bit ray-tracing helper (rthelper/, rt_helper.exe; protocol archive/src/rhi/vulkan/rt_ipc.h) with MSVC + Ninja.
# deploy.ps1 copies it next to the game; tools/instances.py copies it into test instances.
param([string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)  # the repository (this script lives in archive/tools)
$build = Join-Path $root "build/rthelper-x64"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
cmake -S (Join-Path $root 'archive/rthelper') -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" | Out-Null
if ($LASTEXITCODE) { throw 'cmake configure failed' }
cmake --build $build
if ($LASTEXITCODE) { throw 'build failed' }
Write-Host "built $(Join-Path $build 'rt_helper.exe')"
