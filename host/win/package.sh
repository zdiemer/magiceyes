#!/bin/bash
# Package the built bundle into dist/magiceyes-<version>-win64.zip (magiceyes.exe + SDL2.dll +
# README + LICENSE). Uses python3's zipfile so there's no `zip` dependency.
#   usage: host/win/package.sh [version]   (default from MAGICEYES_VERSION, else 0.2.0-dev)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${1:-${MAGICEYES_VERSION:-0.2.0-dev}}"
[ -f "$REPO/bin/magiceyes.exe" ] || { echo "no bin/magiceyes.exe (run build_bundle_win.sh first)"; exit 1; }
[ -f "$REPO/bin/SDL2.dll" ]      || { echo "no bin/SDL2.dll"; exit 1; }
# The FOSS EABI rootfs (Patissier + all Caanoo titles) ships in the bundle so those titles run with
# zero setup. It is built by stage_rootfs_eabi.sh (WSL, EABI_REDIST=1 so no proprietary fonts).
[ -f "$REPO/assets/rootfs-eabi/lib/ld-linux.so.3" ] || {
  echo "no assets/rootfs-eabi (run: EABI_REDIST=1 bash host/win/stage_rootfs_eabi.sh)"; exit 1; }
# The tiny OABI shim (GPH-SDK-built) is overlaid onto an installed Wiz/F100/F200 firmware so its
# titles render into our shm framebuffer; the firmware install flow (firmware.c) copies it.
for f in libSDL-1.2.so.0 libinkadrm.so.0 libdrmcode.so.0; do
  [ -f "$REPO/bin/guest/$f" ] || { echo "no bin/guest/$f (run guest/build_guest.sh)"; exit 1; }
done

NAME="magiceyes-$VERSION-win64"
DIST="$REPO/dist"; STAGE="$DIST/$NAME"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp "$REPO/bin/magiceyes.exe" "$REPO/bin/SDL2.dll" "$STAGE/"
[ -f "$REPO/LICENSE" ] && cp "$REPO/LICENSE" "$STAGE/"
echo "bundling FOSS rootfs-eabi ($(du -sh "$REPO/assets/rootfs-eabi" | cut -f1))"
cp -R "$REPO/assets/rootfs-eabi" "$STAGE/rootfs-eabi"
mkdir -p "$STAGE/overlay-oabi"
cp "$REPO/bin/guest/libSDL-1.2.so.0" "$REPO/bin/guest/libinkadrm.so.0" \
   "$REPO/bin/guest/libdrmcode.so.0" "$STAGE/overlay-oabi/"

cat > "$STAGE/README.txt" <<EOF
magiceyes $VERSION
Run Game Park Holdings GP2X / Wiz games on Windows.

Quick start
  - Double-click magiceyes.exe to open an empty window, then File > Open to pick a game
    (a .gpe, a game folder, or a .zip).
  - Or from a terminal:  magiceyes.exe [options] [game.gpe | folder | game.zip]

Options   -s/--scale N   -f/--fullscreen   --mute   --volume N   --help   --version
Controls  D-pad = arrows;  A/B/X/Y = Z/X/A/S;  Start = Enter;  Select = Backspace;
          L/R = Q/W;  Fullscreen = F11;  Screenshot = F12;  Quit = Esc.

Notes
  - Keep SDL2.dll and the rootfs-eabi\\ and overlay-oabi\\ folders next to magiceyes.exe.
  - F12 saves a screenshot to screenshots\\ next to magiceyes.exe.

What runs out of the box (no setup)
  - GP2X static games and GPEComp self-extractors.
  - EABI homebrew (e.g. Patissier) and Caanoo titles -- these use the bundled FOSS
    rootfs-eabi\\ runtime.

What needs official device firmware (you supply it, legally obtained)
  - Wiz / commercial titles need the device's system libraries; install the firmware via
    Firmware > Install firmware (a Wiz, or GP2X F100/F200, firmware .zip/.img), or run
    magiceyes.exe --install-firmware <firmware.zip|.img>. magiceyes tells you which firmware
    a title needs when you open it.
  - Caanoo titles run without firmware, but on-screen TEXT needs the Caanoo system fonts;
    install the Caanoo firmware the same way for correct text.

magiceyes is free software under the GNU GPL v2 (see LICENSE); the complete source is at
https://github.com/zdiemer/magiceyes . It statically links a fork of the Unicorn CPU
emulator (qemu TCG). SDL2 is under the zlib license. The bundled rootfs-eabi\\ contains
unmodified Free Software libraries from Debian (glibc, libstdc++, SDL 1.2, libpng,
freetype, etc.) under the GNU LGPL/GPL and compatible licenses; their corresponding source
is available from Debian (archive.debian.org, "wheezy" armel) and on written request to the
project. magiceyes ships no device firmware or game data -- supply your own, legally obtained.
EOF

( cd "$DIST" && rm -f "$NAME.zip" && python3 -m zipfile -c "$NAME.zip" "$NAME" )
echo "packaged $DIST/$NAME.zip"
ls -la "$DIST/$NAME.zip"
