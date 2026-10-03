# Compile one or more recovered source files exactly like the hook DLL does, without producing output
# (syntax and type check only). Safe to run from several agents at once.
# Usage: pwsh tools/check_compile.ps1 src/renderer/buffers.cpp [more files]
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$minhook = Get-ChildItem (Join-Path $root 'build') -Recurse -Directory -Filter 'minhook-src' | Select-Object -First 1
$includes = @('hook/src', 'hook/enhance', 'src') | ForEach-Object { '/I' + (Join-Path $root $_) }
if ($minhook) { $includes += '/I' + (Join-Path $minhook.FullName 'include') }
$failed = 0
foreach ($file in $args) {
    $out = & cl /nologo /c /Zs /TP /std:c++20 /EHsc /O2 /MT /arch:IA32 /fp:precise /W4 /permissive- /utf-8 `
        /DWIN32 /D_WINDOWS /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS $includes $file 2>&1
    $problems = $out | Select-String -Pattern ' error | warning '
    if ($LASTEXITCODE -or $problems) { $failed++; $out | Out-Host } else { Write-Host "ok $file" }
}
exit $failed
