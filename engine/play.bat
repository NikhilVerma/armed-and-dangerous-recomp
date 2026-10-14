@echo off
rem Play Armed and Dangerous (lev_1_m1) in the remaster engine. Build first: cargo build --release
rem Every session writes its event log to engine\logs (mission events, sounds and voice lines, hits, shots):
rem session-<time>.txt and sounds-<time>.txt, for comparing a playthrough with the original's logs.
cd /d "%~dp0"
if not exist logs mkdir logs
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set STAMP=%%i
set AIDBG=1
set AND_SOUND_LOG=logs\sounds-%STAMP%.txt
target\release\and_remaster.exe %* > logs\session-%STAMP%.txt 2>&1
