@echo off
rem Play Armed and Dangerous (lev_1_m1) in the remaster engine. Build first: cargo build --release
cd /d "%~dp0"
target\release\and_remaster.exe %*
