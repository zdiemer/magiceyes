#!/bin/bash
# Build the offline GPEComp decompressor CLI. Native host build (Linux/macOS/WSL gcc) or, with
# CC=x86_64-w64-mingw32-gcc, a Windows un-gpecomp.exe. Pure C, no deps.
set -eu
REPO="$(cd "$(dirname "$0")/.." && pwd)"
CC="${CC:-cc}"
OUT="${1:-$REPO/bin/un-gpecomp}"
mkdir -p "$(dirname "$OUT")"
$CC -O2 -Wall -o "$OUT" "$REPO/tools/un-gpecomp.c" "$REPO/host/engine/gpecomp.c" -I"$REPO/host/engine"
echo "built $OUT"
