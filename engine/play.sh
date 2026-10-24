#!/bin/sh
# Play Armed and Dangerous (lev_1_m1) in the remaster engine on macOS or Linux (engine\play.bat on Windows).
# Build first: cargo build --release. Arguments are passed on (e.g. ./play.sh --level lev_2_m1).
# Every session writes its event log to engine/logs (mission events, sounds and voice lines, hits, shots):
# session-<time>.txt and sounds-<time>.txt, for comparing a playthrough with the original's logs.
set -e
cd "$(dirname "$0")"
mkdir -p logs
STAMP=$(date +%Y%m%d-%H%M%S)
export AIDBG=1
export AND_SOUND_LOG="logs/sounds-$STAMP.txt"
exec ./target/release/and_remaster "$@" > "logs/session-$STAMP.txt" 2>&1
