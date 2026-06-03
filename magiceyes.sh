#!/bin/bash
# magiceyes launcher: run a GP2X/Wiz/Caanoo .gpe under qemu-user with our guest
# libs, and show it in the native viewer.
#
#   MAGICEYES_ROOTFS=/path/to/device-rootfs ./magiceyes.sh /path/to/game.gpe
#
# Env:
#   MAGICEYES_ROOTFS  device root filesystem (glibc/SDL/real DRM libs)  [required]
#   MAGICEYES_QEMU    qemu binary  [default: qemu-arm-static]
#   MAGICEYES_SCALE   window scale  [default 3]
#   MAGICEYES_FPS     frame cap     [default 60]
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
GPE="${1:?usage: magiceyes.sh <game.gpe> [args...]}"; shift || true
ROOTFS="${MAGICEYES_ROOTFS:?set MAGICEYES_ROOTFS to the device rootfs}"
QEMU="${MAGICEYES_QEMU:-qemu-arm-static}"
SCALE="${MAGICEYES_SCALE:-3}"
VIEWER="$HERE/bin/viewer"

# ensure our guest libs are staged where the guest loader will find them first
mkdir -p "$ROOTFS/opt/shim"
cp -f "$HERE/bin/guest/"*.so.0 "$ROOTFS/opt/shim/" 2>/dev/null || true
rm -rf "$ROOTFS/dev/shm" 2>/dev/null || true   # so the guest uses host /dev/shm

GDIR="$(cd "$(dirname "$GPE")" && pwd)"
GNAME="$(basename "$GPE")"
cd "$GDIR"
# shadow any bundled device libSDL so ours (in /opt/shim) wins regardless of RPATH
for f in libSDL-1.2.so.0 libSDL-1.2.so.0.11.2; do
  [ -f "$f" ] && [ ! -f "$f.orig" ] && mv -f "$f" "$f.orig"
done
rm -f /dev/shm/gp2x_fb

"$QEMU" -L "$ROOTFS" \
  -E LD_LIBRARY_PATH=/opt/shim:/lib:/usr/lib -E HOME=/tmp \
  -E "FAKESDL_FPS=${MAGICEYES_FPS:-60}" \
  "./$GNAME" "$@" >/tmp/magiceyes_game.log 2>&1 &
GPID=$!
trap 'kill $GPID 2>/dev/null' EXIT
sleep 1
"$VIEWER" "$SCALE"
