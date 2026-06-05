#!/bin/bash
# Cross-build the SINGLE-PROCESS magiceyes.exe (engine + SDL2 viewer in one process) via MinGW
# from WSL/Linux. Collapses the engine<->viewer /dev/shm bridge into in-process memory (g_shm is
# a plain calloc, shared by the viewer worker thread) -> fixes the native-Windows black screen,
# which was two processes failing to attach to the same Local\magiceyes_* named mapping.
# Needs: gcc-mingw-w64-x86-64-posix, the fork built for Windows ($FORK/build-win), and the SDL2
# MinGW devel libs (host/win/get_sdl2.sh).
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
FORK="${ME_UNICORN_FORK:-$HOME/me-unicorn-fork}"
SDLV="${SDLV:-2.30.9}"
SDL="${SDL2_MINGW:-$HOME/sdl2-mingw/SDL2-$SDLV/x86_64-w64-mingw32}"
CC=x86_64-w64-mingw32-gcc
[ -f "$FORK/build-win/libunicorn.a" ] || { echo "no Windows fork lib ($FORK/build-win); run host/win/build_fork_win.sh"; exit 1; }
[ -f "$SDL/include/SDL2/SDL.h" ]      || { echo "no SDL2 mingw at $SDL (run host/win/get_sdl2.sh)"; exit 1; }
mkdir -p "$REPO/bin"
# Optional version stamp for releases: MAGICEYES_VERSION=0.2.0 -> -DME_VERSION="0.2.0".
DEFS=(-DME_BUNDLED)
[ -n "${MAGICEYES_VERSION:-}" ] && DEFS+=(-DME_VERSION="\"${MAGICEYES_VERSION}\"")
# SDL2 is linked dynamically (ship SDL2.dll); libgcc + winpthread are linked statically so the
# only runtime DLL dependency is SDL2.dll.
$CC -O2 -Wall "${DEFS[@]}" -o "$REPO/bin/magiceyes.exe" \
  "$REPO"/host/engine/*.c "$REPO/host/viewer.c" "$REPO/host/win/posix_compat.c" \
  -I "$REPO/host/win/compat" -I "$REPO/host/engine" -I "$FORK/include" \
  -I "$REPO/guest/src" -I "$SDL/include" \
  -L "$SDL/lib" "$FORK/build-win/libunicorn.a" \
  -static-libgcc -Wl,-Bstatic -lpthread -Wl,-Bdynamic \
  -lSDL2 -lm -lws2_32 -lbcrypt -lwinmm -lcomdlg32 -luser32 -lgdi32
cp -f "$SDL/bin/SDL2.dll" "$REPO/bin/"
echo "built $REPO/bin/magiceyes.exe (+ SDL2.dll)"
