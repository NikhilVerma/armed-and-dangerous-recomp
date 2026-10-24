# Single local test entry point (handover s12). Game files cannot leave this machine, so there is no remote CI.
# Usage: pwsh archive/tools/check.ps1 [-Quick]   (-Quick skips the in-game comparison test)
# Suites are added as they come into existence; a suite with no tests yet is reported as skipped.
param([switch]$Quick)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)  # the repository (this script lives in archive/tools)
$failed = 0

function Suite($name, [scriptblock]$body) {
    Write-Host "== $name"
    try { & $body; Write-Host "   ok" } catch { Write-Host "   FAIL: $_"; $script:failed++ }
}

Suite 'repo guard' {
    $tracked = git -C $root ls-files
    $bad = $tracked | Where-Object { $_ -match '^(orig|work|ref)/' -or $_ -match '\.(exe|dll|xbe|big|jit|pcv|raw|cab|iso)$' }
    if ($bad) { throw "forbidden files tracked: $($bad -join ', ')" }
    $hooks = git -C $root config core.hooksPath
    if ($hooks -ne 'tools/hooks') { throw "core.hooksPath is '$hooks', expected tools/hooks" }
}

Suite 'tracker' {
    $csv = Join-Path $root 'symbols/functions.csv'
    $header = (Get-Content $csv -TotalCount 1)
    $expected = 'address,size,name,subsystem,stage,approach,provenance,test,tokens,minutes'
    if ($header -ne $expected) { throw "functions.csv header changed: $header" }
    & (Join-Path $root '.venv/Scripts/python.exe') (Join-Path $root 'archive/tools/tracker_sync.py') --check | Out-Null
    if ($LASTEXITCODE) { throw 'functions.csv out of date: run archive/tools/tracker_sync.py' }
}

foreach ($s in 'boot', 'replay', 'diff', 'formats') {
    $dir = Join-Path $root "tests/$s"
    $tests = Get-ChildItem $dir -File -Exclude '.gitkeep' -ErrorAction SilentlyContinue
    if (-not $tests) { Write-Host "== tests/$s`n   skipped (no tests yet)"; continue }
    if ($s -eq 'formats') {
        Suite "tests/$s" {
            & (Join-Path $root '.venv/Scripts/python.exe') -m pytest -q -x $dir | Select-Object -Last 3 | Out-Host
            if ($LASTEXITCODE) { throw "pytest exit $LASTEXITCODE" }
        }
    }
}

# In-game suites: the comparison runs several scenarios at once in their own game instances (tools/instances.py);
# backend parity runs one scenario at a time in the game folder (it is timing-sensitive). The two suites do not
# overlap: with more games at once, loading timing varies too much for frame-exact comparison.
$py = Join-Path $root '.venv/Scripts/python.exe'
if (-not $Quick) {
    Suite 'comparison (original vs recovered code through the renderer interface, all scenarios)' {
        & $py (Join-Path $root 'tools/compare.py') --rhi --jobs 3 2>&1 | Select-String '^\[|PASS|FAIL|CRASH|timed out|candidate:' | Out-Host
        if ($LASTEXITCODE) { throw "compare.py exit $LASTEXITCODE" }
    }
}

if (-not $Quick) {
    Suite 'vulkan shader generators (vk_shader_test over the game''s shader corpus)' {
        & pwsh -NoProfile -File (Join-Path $root 'archive/tools/build.ps1') -Target vk_shader_test | Out-Null
        if ($LASTEXITCODE) { throw 'vk_shader_test build failed' }
        & (Join-Path $root 'build/hook-release/vk_shader_test.exe') (Join-Path $root 'work/shaders') | Select-Object -Last 1 | Out-Host
        if ($LASTEXITCODE) { throw "vk_shader_test exit $LASTEXITCODE" }
    }
    Suite 'backend parity (D3D9 vs Vulkan: draws per frame and images, archive/tools/compare_backends.py)' {
        & $py (Join-Path $root 'archive/tools/compare_backends.py') --scenario menu,level,play,quit | Select-String '^\[|FAIL|draws:|PASS' | Out-Host
        if ($LASTEXITCODE) { throw "compare_backends.py exit $LASTEXITCODE" }
    }
}

if ($failed) { Write-Host "RED: $failed suite(s) failed"; exit 1 }
Write-Host 'GREEN'
