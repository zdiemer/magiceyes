#!/bin/bash
# Build the native SDL2 viewer for THIS host OS (Linux/macOS via gcc/clang+SDL2,
# Windows via MSYS2/mingw or WSL). Outputs magiceyes/bin/viewer.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ME="$(cd "$HERE/.." && pwd)"
OUT="$ME/bin"
mkdir -p "$OUT"
CC="${CC:-cc}"
"$CC" -O2 -Wall -o "$OUT/viewer" "$HERE/viewer.c" -I "$ME/guest/src" \
  $(pkg-config --cflags --libs sdl2) -lrt
echo "built -> $OUT/viewer"
file "$OUT/viewer" 2>/dev/null || true
