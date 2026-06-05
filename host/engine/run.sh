#!/bin/bash
# Launch the cross-platform engine (forked Unicorn) + SDL2 viewer for a decompressed,
# static GP2X binary. Env passthrough: ME_PROF, ME_GP2X_NOSMCFREEZE, ME_GP2X_SMCLOG, ...
# Engine stderr (incl. ME_PROF fps line) -> $ME_LOG (default /tmp/me_engine.log).
#
#   ME_PROF=1 host/engine/run.sh ~/pbtest/Payback_tmp           # SMC-freeze ON (our fix)
#   ME_PROF=1 ME_GP2X_NOSMCFREEZE=1 host/engine/run.sh ~/pbtest/Payback_tmp   # OFF (baseline)
set -u
BIN="${1:?usage: run.sh <decompressed-binary> [scale]}"
SCALE="${2:-2}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
LOG="${ME_LOG:-/tmp/me_engine.log}"
rm -f /dev/shm/gp2x_fb
cd "$(dirname "$BIN")"
"$REPO/bin/me_unicorn" "$BIN" 2>"$LOG" &
EPID=$!
sleep 0.4
"$REPO/bin/viewer" "$SCALE"
kill "$EPID" 2>/dev/null
echo "engine log: $LOG  (grep PROF for fps)"
