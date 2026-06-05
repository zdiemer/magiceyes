#!/bin/bash
# Package the built bundle into dist/magiceyes-<version>-win64.zip (magiceyes.exe + SDL2.dll +
# README + LICENSE). Uses python3's zipfile so there's no `zip` dependency.
#   usage: host/win/package.sh [version]   (default from MAGICEYES_VERSION, else 0.2.0-dev)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${1:-${MAGICEYES_VERSION:-0.2.0-dev}}"
[ -f "$REPO/bin/magiceyes.exe" ] || { echo "no bin/magiceyes.exe (run build_bundle_win.sh first)"; exit 1; }
[ -f "$REPO/bin/SDL2.dll" ]      || { echo "no bin/SDL2.dll"; exit 1; }

NAME="magiceyes-$VERSION-win64"
DIST="$REPO/dist"; STAGE="$DIST/$NAME"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp "$REPO/bin/magiceyes.exe" "$REPO/bin/SDL2.dll" "$STAGE/"
[ -f "$REPO/LICENSE" ] && cp "$REPO/LICENSE" "$STAGE/"

cat > "$STAGE/README.txt" <<EOF
magiceyes $VERSION
Run Game Park Holdings GP2X / Wiz games on Windows.

Quick start
  - Double-click magiceyes.exe to open an empty window, then File > Open to pick a game
    (a .gpe, a game folder, or a .zip).
  - Or from a terminal:  magiceyes.exe [options] [game.gpe | folder | game.zip]

Options   -s/--scale N   -f/--fullscreen   --mute   --volume N   --help   --version
Controls  D-pad = arrows;  A/B/X/Y = Z/X/A/S;  Start = Enter;  Select = Backspace;
          L/R = Q/W;  Fullscreen = F11;  Quit = Esc.

Notes
  - Keep SDL2.dll next to magiceyes.exe.
  - GP2X static games and GPEComp self-extractors are supported. Dynamically-linked
    titles (e.g. Wiz) are detected but not yet runnable in this native build.

magiceyes is free software under the GNU GPL v2 (see LICENSE); it statically links a fork
of the Unicorn CPU emulator (qemu TCG). SDL2 is under the zlib license.
EOF

( cd "$DIST" && rm -f "$NAME.zip" && python3 -m zipfile -c "$NAME.zip" "$NAME" )
echo "packaged $DIST/$NAME.zip"
ls -la "$DIST/$NAME.zip"
