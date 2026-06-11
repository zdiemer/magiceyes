#!/bin/bash
# Build the magiceyes Unicorn engine (host/engine/*.c) against the forked Unicorn.
# Env: ME_UNICORN_FORK (default ~/me-unicorn-fork).
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
FORK="${ME_UNICORN_FORK:-$HOME/me-unicorn-fork}"
[ -f "$FORK/build/libunicorn.a" ] || { echo "fork not built at $FORK (see fork-patches/README.md)"; exit 1; }
# ME_DEBUG_BUILD=1 -> a separate AddressSanitizer build (bin/me_unicorn_dbg) for headless triage
# of memory bugs (the reload crash, guest-pointer overruns). Slower; not the shipping binary.
if [ -n "${ME_DEBUG_BUILD:-}" ]; then
  OUT="$REPO/bin/me_unicorn_dbg"; OPT="-O0 -g -fsanitize=address -fno-omit-frame-pointer"
else
  OUT="$REPO/bin/me_unicorn";     OPT="-O2"
fi
mkdir -p "$(dirname "$OUT")"   # bin/ is gitignored -> absent on a clean CI checkout
cc $OPT -Wall -o "$OUT" "$REPO"/host/engine/*.c "$REPO"/host/engine/extract/*.c \
  -I "$REPO/host/engine" -I "$FORK/include" -I "$REPO/guest/src" \
  "$FORK/build/libunicorn.a" -lpthread -lm -lrt
echo "built $OUT (engine: host/engine/*.c, fork: $FORK)"
