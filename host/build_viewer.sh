#!/bin/bash
# Build the native SDL2 viewer for THIS host OS (Linux/macOS via gcc/clang+SDL2,
# Windows via MSYS2/mingw or WSL). Outputs magiceyes/bin/viewer.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ME="$(cd "$HERE/.." && pwd)"
OUT="$ME/bin"
mkdir -p "$OUT"
CC="${CC:-cc}"
# state_file.c is the savestate CONTAINER: the slot picker in this binary reads a state's
# thumbnail and timestamp through it, with no engine linked. -DMST_NO_COMPRESS because this
# build has no miniz; META and THMB are always written STORED precisely so a picker still works.
"$CC" -O2 -Wall -DMST_NO_COMPRESS -o "$OUT/viewer" "$HERE/viewer.c" "$HERE/png_write.c" \
  "$HERE/input_config.c" "$HERE/settings_ui.c" "$HERE/state_file.c" \
  -I "$ME/guest/src" -I "$HERE" \
  $(pkg-config --cflags --libs sdl2) -lrt
echo "built -> $OUT/viewer"
file "$OUT/viewer" 2>/dev/null || true
