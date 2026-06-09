#!/bin/bash
# Materialise a Windows-readable device rootfs for the native engine's dynamic-linker path.
#
# The extracted Wiz/GP2X rootfs (assets/rootfs/0/rootfs) uses Linux SYMLINKS for every
# versioned lib (libc.so.6 -> libc-2.3.6.so, /lib/ld-linux.so.2 -> ld-2.3.6.so, ...). Those
# are stored on NTFS as WSL-style symlinks that NATIVE Windows open() cannot follow ("the file
# cannot be accessed by the system"), so the engine's MinGW runtime can't load them.
#
# This produces a dereferenced copy (real files, no symlinks) under assets/rootfs-win/ that the
# native engine points ME_GP2X_ROOTFS at. Dynamic SDL games run on the firmware's OWN real libSDL
# (it renders through the engine's emulated /dev/fb0 + MMSP2/Pollux registers, exactly like the
# firmware menu) -- we no longer overlay the brittle fake-SDL shim here. Only the Inka DRM gate is
# stubbed. The shim stays for the qemu backend (which can't emulate the hardware).
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

# NOTE: we deliberately keep the firmware's REAL libSDL (cp -RL above already dereferenced it). The
# real Wiz/GP2X libSDL renders through the engine's emulated framebuffer + MMSP2/Pollux registers
# (the same path the firmware menu uses) and matches the exact ABI the firmware's SDL_image/_ttf/
# _mixer were built against -- so no white-rectangle/blit corruption. (Do NOT overlay the shim.)

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
