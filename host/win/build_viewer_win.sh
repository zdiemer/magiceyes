#!/bin/bash
# Cross-build the SDL2 viewer as a native Windows .exe (MinGW). Needs the SDL2 mingw devel
# libs (host/win/_getsdl.sh fetches them). Links SDL2 dynamically; ships SDL2.dll next to it.
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
CC=x86_64-w64-mingw32-gcc
SDLV="${SDLV:-2.30.9}"
T="${SDL2_MINGW:-$HOME/sdl2-mingw/SDL2-$SDLV/x86_64-w64-mingw32}"
[ -f "$T/include/SDL2/SDL.h" ] || { echo "no SDL2 mingw at $T (run host/win/_getsdl.sh)"; exit 1; }
mkdir -p "$REPO/bin"
$CC -O2 -Wall -o "$REPO/bin/viewer.exe" \
  "$REPO/host/viewer.c" "$REPO/host/png_write.c" \
  "$REPO/host/input_config.c" "$REPO/host/settings_ui.c" "$REPO/host/win/posix_compat.c" \
  -I "$REPO/host/win/compat" -I "$REPO/guest/src" -I "$T/include" \
  -L "$T/lib" -lmingw32 -lSDL2main -lSDL2 -lm -lwinmm
cp -f "$T/bin/SDL2.dll" "$REPO/bin/"
echo "built $REPO/bin/viewer.exe (+ SDL2.dll)"
