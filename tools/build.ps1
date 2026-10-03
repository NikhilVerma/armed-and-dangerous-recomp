# Build the 32-bit hook DLL (hook/) with MSVC + Ninja.
# Usage: pwsh tools/build.ps1 [-Config Release|Debug] [-Clean]
param([string]$Config = 'Release', [switch]$Clean)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build/hook-$($Config.ToLower())"

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null

if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }
cmake -S (Join-Path $root 'hook') -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" | Out-Null
if ($LASTEXITCODE) { throw 'cmake configure failed' }
cmake --build $build
if ($LASTEXITCODE) { throw 'build failed' }
Write-Host "built $build\dinput8.dll"
