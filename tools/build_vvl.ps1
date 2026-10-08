# Build the Khronos Vulkan validation layer (32-bit, for the game process) from source, for debugging the Vulkan
# backend: build/vvl (a shallow clone of KhronosGroup/Vulkan-ValidationLayers at the tag of our Vulkan headers),
# output build/vvl-x86/layers/Release. Run the game with:
#   $env:VK_LAYER_PATH = "<root>/build/vvl-x86/layers/Release"; $env:VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation"
# A debugging tool only: nothing from it ships.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root 'build/vvl'
$build = Join-Path $root 'build/vvl-x86'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
cmake -S $src -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUPDATE_DEPS=ON -DBUILD_TESTS=OFF
if ($LASTEXITCODE) { throw 'configure failed' }
cmake --build $build --config Release
if ($LASTEXITCODE) { throw 'build failed' }
Write-Host "built $build"
