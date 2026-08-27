#!/bin/bash
# Package the built bundle into dist/magiceyes-<version>-win64.zip (magiceyes.exe + SDL2.dll +
# README + LICENSE). Uses python3's zipfile so there's no `zip` dependency.
#   usage: host/win/package.sh [version]   (default from MAGICEYES_VERSION, else 0.2.0-dev)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${1:-${MAGICEYES_VERSION:-0.2.0-dev}}"
[ -f "$REPO/bin/magiceyes.exe" ] || { echo "no bin/magiceyes.exe (run build_bundle_win.sh first)"; exit 1; }
[ -f "$REPO/bin/SDL2.dll" ]      || { echo "no bin/SDL2.dll"; exit 1; }
# The EABI rootfs (Patissier + all Caanoo titles) ships in the bundle so those titles run with zero
# setup. Built by stage_rootfs_eabi.sh (WSL). We add the Caanoo system fonts below.
[ -f "$REPO/assets/rootfs-eabi/lib/ld-linux.so.3" ] || {
  echo "no assets/rootfs-eabi (run: bash host/win/stage_rootfs_eabi.sh)"; exit 1; }
# The OABI runtime (firmware glibc-2.3.6 + the device's REAL libSDL + helpers, deref'd for native
# Windows) ships too, so Wiz/GP2X dynamic titles run firmware-free on the real libSDL. Built by
# stage_rootfs.sh from a firmware image. (The firmware is freely redistributable; see
# third_party/LICENSES/firmware-redistribution.txt.)
[ -f "$REPO/assets/rootfs-win/lib/ld-linux.so.2" ] || {
  echo "no assets/rootfs-win (run: bash host/win/stage_rootfs.sh)"; exit 1; }
# Caanoo system fonts (QType4/DGE TrueType): freely redistributable, ship them so Caanoo title text
# works without a firmware install.
CAANOO_FONTS="$REPO/assets/caanoo-ref/usr/gp2x"
[ -d "$CAANOO_FONTS" ] || { echo "no Caanoo fonts at $CAANOO_FONTS (run host/win/extract_caanoo_fw.sh)"; exit 1; }
# The tiny OABI shim (GPH-SDK-built) is still overlaid onto an installed firmware for the qemu
# backend / firmware-menu DRM gate; the firmware install flow (firmware.c) copies it.
for f in libSDL-1.2.so.0 libinkadrm.so.0 libdrmcode.so.0; do
  [ -f "$REPO/bin/guest/$f" ] || { echo "no bin/guest/$f (run guest/build_guest.sh)"; exit 1; }
done

# The bundled runtime is gitignored and staged by hand (stage_release_assets.sh), so nothing
# relates it to the source it was built from. That is how v0.5.0 shipped a four-month-old runtime.
# The stamp closes it: if guest/src has moved since the last restage, the staged stubs in this
# tarball are stale and the zip would go out missing whatever changed. ME_SKIP_ASSET_STAMP=1
# overrides for a local throwaway build.
STAMP="$REPO/host/win/release-assets.stamp"
if [ -z "${ME_SKIP_ASSET_STAMP:-}" ] && [ -f "$STAMP" ]; then
  want=$(awk '$1=="guest_src"{print $2}' "$STAMP")
  have=$( cd "$REPO" && find guest/src -type f -print0 | sort -z | xargs -0 sha256sum \
            | sha256sum | cut -d" " -f1 )
  if [ -n "$want" ] && [ "$want" != "$have" ]; then
    echo "guest/src has changed since the staged runtime was built:" >&2
    echo "  staged from $want" >&2
    echo "  working tree $have" >&2
    echo "Rebuild the stubs (guest/build_guest.sh) and restage" >&2
    echo "(host/win/stage_release_assets.sh), or set ME_SKIP_ASSET_STAMP=1 for a throwaway." >&2
    exit 1
  fi
fi

NAME="magiceyes-$VERSION-win64"
DIST="$REPO/dist"; STAGE="$DIST/$NAME"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp "$REPO/bin/magiceyes.exe" "$REPO/bin/SDL2.dll" "$STAGE/"
[ -f "$REPO/LICENSE" ] && cp "$REPO/LICENSE" "$STAGE/"
echo "bundling rootfs-eabi ($(du -sh "$REPO/assets/rootfs-eabi" | cut -f1))"
cp -R "$REPO/assets/rootfs-eabi" "$STAGE/rootfs-eabi"
# Stage the Caanoo system fonts into a device-scoped caanoo-fonts\ dir so /usr/gp2x/*.ttf resolves
# firmware-free (caanoo_font_overlay, syscalls.c) -- a Caanoo title can be OABI or EABI, so the font
# lives outside either rootfs.
mkdir -p "$STAGE/caanoo-fonts/usr/gp2x"
cp -f "$CAANOO_FONTS"/*.ttf "$STAGE/caanoo-fonts/usr/gp2x/"
echo "bundling Caanoo fonts ($(du -sh "$CAANOO_FONTS" | cut -f1), $(ls "$CAANOO_FONTS"/*.ttf | wc -l) ttf)"
echo "bundling rootfs-win OABI runtime ($(du -sh "$REPO/assets/rootfs-win" | cut -f1), real libSDL)"
cp -R "$REPO/assets/rootfs-win" "$STAGE/rootfs-win"
mkdir -p "$STAGE/overlay-oabi"
cp "$REPO/bin/guest/libSDL-1.2.so.0" "$REPO/bin/guest/libinkadrm.so.0" \
   "$REPO/bin/guest/libdrmcode.so.0" "$STAGE/overlay-oabi/"

# Both rootfs trees carry a byte-identical 32MB timidity patch set, and a zip cannot share a
# file between two paths, so the download paid for it twice. Ship one copy under shared/ and
# let shared_asset_overlay (syscalls.c) resolve /usr/share/midi for whichever rootfs a title
# runs under. Refuse rather than guess if the two ever stop matching.
EABI_MIDI="$STAGE/rootfs-eabi/usr/share/midi"
WIN_MIDI="$STAGE/rootfs-win/usr/share/midi"
if [ -d "$EABI_MIDI" ] && [ -d "$WIN_MIDI" ]; then
  if ! diff -qr "$EABI_MIDI" "$WIN_MIDI" >/dev/null 2>&1; then
    echo "rootfs-eabi and rootfs-win MIDI trees differ; not safe to share one copy" >&2
    exit 1
  fi
  mkdir -p "$STAGE/shared/usr/share"
  mv "$WIN_MIDI" "$STAGE/shared/usr/share/midi"
  rm -rf "$EABI_MIDI"
  echo "shared MIDI patches ($(du -sh "$STAGE/shared/usr/share/midi" | cut -f1), was in both trees)"
fi

# The Caanoo system fonts are already served device-scoped from caanoo-fonts/ by
# caanoo_font_overlay, which runs BEFORE the rootfs lookup, so the copies inside rootfs-eabi
# are never the ones that resolve. 9MB each, and TTFs barely deflate.
if [ -d "$STAGE/rootfs-eabi/usr/gp2x" ]; then
  rm -f "$STAGE/rootfs-eabi/usr/gp2x"/*.ttf
  rmdir "$STAGE/rootfs-eabi/usr/gp2x" 2>/dev/null || true
  echo "dropped the duplicate Caanoo fonts from rootfs-eabi (caanoo-fonts/ serves them)"
fi

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
  - Keep SDL2.dll and the rootfs-eabi\\, rootfs-win\\, caanoo-fonts\\ and overlay-oabi\\
    folders next to magiceyes.exe.
  - F12 saves a screenshot to screenshots\\ next to magiceyes.exe.

Games run with no setup -- no firmware install needed
  - GP2X static games and GPEComp self-extractors.
  - Wiz / GP2X commercial + dynamic titles run on the bundled rootfs-win\\ runtime (the real
    device libSDL).
  - EABI homebrew and Caanoo titles (incl. their on-screen text) run on the bundled
    rootfs-eabi\\ runtime + Caanoo fonts.

What still needs official device firmware (you supply it, legally obtained)
  - Booting the device's own firmware MENU (Firmware > Boot firmware): install a Wiz / GP2X
    F100/F200 / Caanoo firmware via Firmware > Install firmware (or
    magiceyes.exe --install-firmware <firmware.zip|.img>). Running games does not need this.

magiceyes is free software under the GNU GPL v2 (see LICENSE); the complete source is at
https://github.com/zdiemer/magiceyes . It statically links a fork of the Unicorn CPU
emulator (qemu TCG). SDL2 is under the zlib license. The bundled rootfs-eabi\\ contains
unmodified Free Software libraries from Debian (glibc, libstdc++, SDL 1.2, libpng, freetype,
etc.) under the GNU LGPL/GPL; their source is available from Debian (archive.debian.org,
"wheezy" armel). The bundled rootfs-win\\ runtime and Caanoo fonts come from the GP2X / Wiz /
Caanoo device firmware, which Game Park Holdings distributed freely; the libraries are LGPL
(SDL, glibc) and the GPH SDK material is under a permissive zlib-style license (see
third_party/LICENSES/). magiceyes ships no proprietary firmware UI (gp2xmenu/themes) or game
data -- supply your own, legally obtained, to boot the firmware menu.
EOF

( cd "$DIST" && rm -f "$NAME.zip" && python3 -m zipfile -c "$NAME.zip" "$NAME" )
echo "packaged $DIST/$NAME.zip"
ls -la "$DIST/$NAME.zip"
