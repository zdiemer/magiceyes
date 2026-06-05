#!/bin/bash
# Build the magiceyes Unicorn engine (host/engine/*.c) against the forked Unicorn.
# Env: ME_UNICORN_FORK (default ~/me-unicorn-fork).
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
FORK="${ME_UNICORN_FORK:-$HOME/me-unicorn-fork}"
[ -f "$FORK/build/libunicorn.a" ] || { echo "fork not built at $FORK (see fork-patches/README.md)"; exit 1; }
cc -O2 -Wall -o "$REPO/bin/me_unicorn" "$REPO"/host/engine/*.c \
  -I "$REPO/host/engine" -I "$FORK/include" -I "$REPO/guest/src" \
  "$FORK/build/libunicorn.a" -lpthread -lm -lrt
echo "built $REPO/bin/me_unicorn (engine: host/engine/*.c, fork: $FORK)"
