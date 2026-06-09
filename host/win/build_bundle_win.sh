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
# -mwindows = GUI subsystem: double-clicking spawns NO stray console window. main() is still the
# entry point (MinGW's GUI CRT calls main; the viewer already sets SDL_MAIN_HANDLED). For terminal
# launches, me_platform_init() AttachConsole()s the parent so --help/--version/diagnostics still show.
# ME_DEV_BUILD=1 -> magiceyes-dev.exe, a CONSOLE-subsystem build (no -mwindows): stderr/ME_TRACE
# go straight to the terminal (the GUI window still opens), so the engine's diagnostics are visible
# while debugging -- the -mwindows release build has no stderr.
if [ -n "${ME_DEV_BUILD:-}" ]; then OUT="$REPO/bin/magiceyes-dev.exe"; SUBSYS=(); DEFS+=(-DME_DEV);
else OUT="$REPO/bin/magiceyes.exe"; SUBSYS=(-mwindows); fi
$CC -O2 -Wall "${SUBSYS[@]}" "${DEFS[@]}" -o "$OUT" \
  "$REPO"/host/engine/*.c "$REPO"/host/engine/extract/*.c "$REPO/host/viewer.c" "$REPO/host/png_write.c" \
  "$REPO/host/input_config.c" "$REPO/host/settings_ui.c" "$REPO/host/win/posix_compat.c" \
  -I "$REPO/host/win/compat" -I "$REPO/host/engine" -I "$FORK/include" \
  -I "$REPO/guest/src" -I "$SDL/include" \
  -L "$SDL/lib" "$FORK/build-win/libunicorn.a" \
  -static-libgcc -Wl,-Bstatic -lpthread -Wl,-Bdynamic \
  -lSDL2 -lm -lws2_32 -lbcrypt -lwinmm -lcomdlg32 -luser32 -lgdi32 -lshell32 -lole32
cp -f "$SDL/bin/SDL2.dll" "$REPO/bin/"
echo "built $OUT (+ SDL2.dll)"

# Keep the DRM gate stubs in rootfs-win in sync with the just-built bin/guest stubs. We no longer
# overlay the fake-SDL shim here -- dynamic titles run on the firmware's REAL libSDL that
# stage_rootfs.sh leaves in place (rendered via the engine's emulated framebuffer + MMSP2/Pollux),
# which fixes the shim's blit/surface ABI corruption. Only libinkadrm/libdrmcode are stubbed (the
# Inka DRM gate). (The shim is still built into bin/guest for the qemu backend.)
RWIN="$REPO/assets/rootfs-win/lib"
if [ -d "$RWIN" ]; then
  for base in libinkadrm libdrmcode; do
    s="$REPO/bin/guest/$base.so.0"; [ -f "$s" ] && for n in "$base.so.0" "$base.so.0.0.0"; do cp -f "$s" "$RWIN/$n"; done
  done
  # fake-GLES offload over the Pollux GLES sonames (OABI Caanoo titles -> engine GL backend)
  GL="$REPO/bin/guest/libGLESv1_CM.so"
  if [ -f "$GL" ]; then for n in libopengles_lite.so libopengles_lite.so.0 libopengles_lite.so.0.0.0 \
       libglport.so libglport.so.0 libglport.so.0.0.0 libGLESv1_CM.so libOpenEGL.so libEGL.so libGLESv2.so; do
       cp -f "$GL" "$RWIN/$n"; done; fi
  echo "refreshed DRM gate stubs + fake-GLES -> $RWIN (real libSDL kept; shim not overlaid)"
fi
