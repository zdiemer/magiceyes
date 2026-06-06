#!/bin/bash
# Materialise a Windows-readable device rootfs for the native engine's dynamic-linker path.
#
# The extracted Wiz/GP2X rootfs (assets/rootfs/0/rootfs) uses Linux SYMLINKS for every
# versioned lib (libc.so.6 -> libc-2.3.6.so, /lib/ld-linux.so.2 -> ld-2.3.6.so, ...). Those
# are stored on NTFS as WSL-style symlinks that NATIVE Windows open() cannot follow ("the file
# cannot be accessed by the system"), so the engine's MinGW runtime can't load them.
#
# This produces a dereferenced copy (real files, no symlinks) under assets/rootfs-win/ that the
# native engine points ME_GP2X_ROOTFS at, and stages our fake-SDL shim in place of the device
# libSDL so dynamic SDL games render into the engine's shm framebuffer (the W&W/Odonata path).
#
# Run from WSL/Linux (symlinks resolve there): bash host/win/stage_rootfs.sh
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="${MAGICEYES_ROOTFS:-$REPO/assets/rootfs/0/rootfs}"
DST="${1:-$REPO/assets/rootfs-win}"
[ -e "$SRC/lib/ld-linux.so.2" ] || { echo "no rootfs at $SRC"; exit 1; }

echo "staging $SRC -> $DST (dereferencing symlinks)"
rm -rf "$DST"; mkdir -p "$DST"
# Only the dirs the dynamic linker + libs need; game assets come from the game's own dir.
for d in lib usr/lib etc; do
  [ -d "$SRC/$d" ] || continue
  mkdir -p "$DST/$d"
  # -L dereferences symlinks into real file copies; -R recurses. Skip dangling links.
  cp -RL "$SRC/$d/." "$DST/$d/" 2>/dev/null || true
done

# Stage the fake-SDL shim in place of the device libSDL so the guest ld.so loads OURS:
# it renders into the gp2x_fb shm the viewer reads (instead of the device's Pollux/MMSP2 SDL).
SHIM="$REPO/bin/guest/libSDL-1.2.so.0"
if [ -f "$SHIM" ]; then
  for name in libSDL-1.2.so.0 libSDL-1.2.so.0.11.2; do cp -f "$SHIM" "$DST/lib/$name"; done
  echo "staged fake-SDL shim -> $DST/lib/libSDL-1.2.so.0"
else
  echo "WARNING: no shim at $SHIM (build it with guest/build_guest.sh); dynamic SDL games won't render"
fi

# Stage the DRM gate stubs over the firmware's real libinkadrm/libdrmcode. The real
# libinkadrm getserial() reads the handset serial from /dev/i2c-0 and, with no device,
# the title bails straight back to gp2xmenu ("crashes instantly", e.g. Deicide 3). Our
# stubs satisfy the boot gate. Cover every soname the games' DT_NEEDED can ask for.
for base in libinkadrm libdrmcode; do
  STUB="$REPO/bin/guest/$base.so.0"
  [ -f "$STUB" ] || { echo "WARNING: no DRM stub at $STUB (build with guest/build_guest.sh)"; continue; }
  for name in "$base.so" "$base.so.0" "$base.so.0.0.0"; do
    [ -e "$DST/lib/$name" ] && cp -f "$STUB" "$DST/lib/$name"
  done
  cp -f "$STUB" "$DST/lib/$base.so.0"   # ensure the canonical soname exists
done
echo "staged DRM gate stubs (libinkadrm/libdrmcode)"

echo "done. files:"
ls "$DST/lib" | wc -l
echo "ME_GP2X_ROOTFS=$DST"
