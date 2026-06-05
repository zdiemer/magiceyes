#!/bin/bash
set -e
V=2.30.9
DEST="$HOME/sdl2-mingw"
if [ ! -f "$DEST/SDL2-$V/x86_64-w64-mingw32/include/SDL2/SDL.h" ]; then
  mkdir -p "$DEST" && cd "$DEST"
  echo "downloading SDL2 $V mingw devel..."
  wget -q "https://github.com/libsdl-org/SDL/releases/download/release-$V/SDL2-devel-$V-mingw.tar.gz" -O sdl2.tar.gz
  tar xzf sdl2.tar.gz && rm -f sdl2.tar.gz
fi
T="$DEST/SDL2-$V/x86_64-w64-mingw32"
echo "=== SDL2 mingw tree ==="
[ -f "$T/include/SDL2/SDL.h" ] && echo "  headers OK: $T/include"
[ -f "$T/lib/libSDL2.a" ] && echo "  static lib OK: $T/lib/libSDL2.a"
[ -f "$T/bin/SDL2.dll" ] && echo "  runtime DLL OK: $T/bin/SDL2.dll"
