# Single local test entry point (handover s12). Game files cannot leave this machine, so there is no remote CI.
# Usage: pwsh tools/check.ps1 [-Quick]   (-Quick skips the in-game comparison test)
# Suites are added as they come into existence; a suite with no tests yet is reported as skipped.
param([switch]$Quick)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
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
    & (Join-Path $root '.venv/Scripts/python.exe') (Join-Path $root 'tools/tracker_sync.py') --check | Out-Null
    if ($LASTEXITCODE) { throw 'functions.csv out of date: run tools/tracker_sync.py' }
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

if (-not $Quick) {
    Suite 'comparison (original vs recovered, menu + level scenarios)' {
        $py = Join-Path $root '.venv/Scripts/python.exe'
        & $py (Join-Path $root 'tools/compare.py') | Out-Host
        if ($LASTEXITCODE) { throw "compare.py exit $LASTEXITCODE" }
    }
}

if ($failed) { Write-Host "RED: $failed suite(s) failed"; exit 1 }
Write-Host 'GREEN'
