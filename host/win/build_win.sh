#!/bin/bash
# Cross-build the magiceyes engine as a native Windows .exe (MinGW from WSL/Linux).
# Needs: gcc-mingw-w64-x86-64-posix, and the fork built for Windows at $FORK/build-win.
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
FORK="${ME_UNICORN_FORK:-$HOME/me-unicorn-fork}"
CC=x86_64-w64-mingw32-gcc
[ -f "$FORK/build-win/libunicorn.a" ] || { echo "no Windows fork lib ($FORK/build-win); run host/win/build_fork_win.sh"; exit 1; }
mkdir -p "$REPO/bin"
$CC -O2 -Wall -o "$REPO/bin/me_unicorn.exe" \
  "$REPO"/host/engine/*.c "$REPO"/host/engine/extract/*.c "$REPO/host/win/posix_compat.c" \
  -I "$REPO/host/win/compat" -I "$REPO/host/engine" -I "$FORK/include" -I "$REPO/guest/src" \
  "$FORK/build-win/libunicorn.a" \
  -lpthread -lm -lws2_32 -lbcrypt -lwinmm -static
echo "built $REPO/bin/me_unicorn.exe"
