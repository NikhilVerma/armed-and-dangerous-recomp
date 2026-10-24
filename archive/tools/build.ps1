# Build the 32-bit hook DLL (hook/) with MSVC + Ninja.
# Usage: pwsh archive/tools/build.ps1 [-Config Release|Debug] [-Clean] [-BuildDir build/x] [-Target vk_shader_test]
#        [-Exclude "ai_06,game_01"]  (regexes: src/ files still being written by recovery agents are left out)
# Parallel agents each pass their own -BuildDir (one build tree must not be built by two processes at once).
param([string]$Config = 'Release', [switch]$Clean, [string]$BuildDir = '', [string]$Target = '', [string]$Exclude = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)  # the repository (this script lives in archive/tools)
$build = if ($BuildDir) { Join-Path $root $BuildDir } else { Join-Path $root "build/hook-$($Config.ToLower())" }

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null

if (-not (Test-Path (Join-Path $root 'archive/src/gen/orig_calls.h'))) {
    Write-Host 'generating archive/src/gen/ (archive/tools/abi.py, ~3 min)'
    & (Join-Path $root '.venv/Scripts/python.exe') (Join-Path $root 'archive/tools/abi.py')
    if ($LASTEXITCODE) { throw 'abi generation failed' }
}
if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }
$excludeList = @($Exclude -split ',' | ForEach-Object { $_.Trim().Trim([char]39).Trim([char]34) } | Where-Object { $_ })
cmake -S (Join-Path $root 'archive/hook') -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DAND_EXCLUDE=$($excludeList -join ';')" | Out-Null
if ($LASTEXITCODE) { throw 'cmake configure failed' }
if ($Target) { cmake --build $build --target $Target } else { cmake --build $build }
if ($LASTEXITCODE) { throw 'build failed' }
Write-Host "built $build\$(if ($Target) { $Target } else { 'dinput8.dll' })"
