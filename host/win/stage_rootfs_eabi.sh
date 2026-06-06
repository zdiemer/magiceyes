#!/bin/bash
# Materialise the EABI (ld-linux.so.3) device rootfs for CodeSourcery-built Wiz homebrew
# (Patissier / rg_ura). Counterpart to stage_rootfs.sh, which stages the FIRMWARE rootfs
# (glibc-2.3.6, ld-linux.so.2, for the commercial titles Deicide 3 / Her Knights / Odonata).
#
# Why a second rootfs: open-SDK homebrew is built with a mainline ARM EABI toolchain whose
# glibc baselines symbol versions at GLIBC_2.4 and names its interpreter /lib/ld-linux.so.3.
# The firmware's GPH glibc-2.3.6 uses GLIBC_2.2 versions and ld-linux.so.2 -- the two are
# ABI-incompatible (conflicting libc.so.6), so they can't be merged. The engine picks the
# right rootfs per title from its PT_INTERP (syscalls.c me_rootfs_select).
#
# This assembles rootfs-eabi entirely from Debian **Wheezy** armel (glibc 2.13, min-kernel
# 2.6.26 -- the engine reports 2.6.32 so it passes) + a fake-SDL shim freshly cross-compiled
# for EABI (the GPH-built shim refs GLIBC_2.2 and is unusable here). rg only CALLS SDL(shim),
# SDL_mixer (OGG bgm), and SDL_gfx; its other NEEDED libs just have to LOAD, so the unused
# leaf decoders/libs are empty soname stubs.
#
# Run from WSL/Linux (needs: arm-linux-gnueabi-gcc, dpkg-deb, curl, readelf):
#   bash host/win/stage_rootfs_eabi.sh
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DST="${1:-$REPO/assets/rootfs-eabi}"
SDK="${MAGICEYES_SDK:-$REPO/assets/sdk/GPH_SDK}"
CC="${CC:-arm-linux-gnueabi-gcc}"
BASE="${DEBIAN_MIRROR:-http://archive.debian.org/debian}"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
MA=arm-linux-gnueabi
command -v "$CC" >/dev/null || { echo "need $CC (apt install gcc-arm-linux-gnueabi)"; exit 1; }
[ -d "$SDK/DGE/include/SDL" ] || { echo "need GPH SDK SDL headers at $SDK/DGE/include/SDL"; exit 1; }

echo "== fetch Wheezy armel Packages index =="
curl -sfL "$BASE/dists/wheezy/main/binary-armel/Packages.gz" -o "$W/P.gz"
deb_path(){ zcat "$W/P.gz" | awk -v p="$1" '$1=="Package:"{w=($2==p)} w&&$1=="Filename:"{print $2;exit}'; }
fetch(){ local f="$W/$1.deb"; curl -sfL "$BASE/$(deb_path "$1")" -o "$f"; echo "$f"; }

echo "== dev sysroot (pins the shim's symbol refs to <= glibc 2.13) =="
SYS="$W/sys"; mkdir -p "$SYS"
for p in libc6 libc6-dev linux-libc-dev; do dpkg-deb -x "$(fetch $p)" "$SYS"; done

echo "== cross-compile the EABI fake-SDL shim (armv5te soft-float, Wheezy headers) =="
SH="$W/shim"; mkdir -p "$SH/inc"; cp -rf "$SDK/DGE/include/." "$SH/inc/"
GCCINC="$($CC -print-file-name=include)"
$CC -shared -fPIC -O2 -march=armv5te -marm -mfloat-abi=soft -fno-stack-protector -D_FORTIFY_SOURCE=0 \
   --sysroot="$SYS" -B "$SYS/usr/lib/$MA" -L "$SYS/usr/lib/$MA" -L "$SYS/lib/$MA" \
   -nostdinc -I "$SH/inc/SDL" -I "$SH/inc" -I "$REPO/guest/src" \
   -isystem "$GCCINC" -isystem "$SYS/usr/include/$MA" -isystem "$SYS/usr/include" \
   -Wl,-soname,libSDL-1.2.so.0 -o "$SH/libSDL-1.2.so.0" "$REPO/guest/src/fakesdl.c" -lrt -ldl
bad=$(readelf -W --dyn-syms "$SH/libSDL-1.2.so.0" | awk '$7=="UND"{print $8}' \
      | grep -E '__gettimeofday64|__nanosleep64|__dlopen|GLIBC_2\.(1[4-9]|[2-9][0-9])' || true)
[ -z "$bad" ] || { echo "shim has too-new refs: $bad"; exit 1; }

echo "== extract runtime libs =="
ST="$W/stage"; mkdir -p "$ST"
#   core: libc/ld/libm/libpthread/libdl/librt, C++ runtime, zlib
#   SDL_mixer + its real OGG decoder chain (bgm is .ogg) + mod/flac/mad (NEEDED, so must load)
#   SDL_gfx (rotozoomSurface)
for p in libc6 libgcc1 libstdc++6 zlib1g \
         libsdl-mixer1.2 libsdl-gfx1.2-4 \
         libvorbisfile3 libvorbis0a libogg0 libmikmod2 libmad0 libflac8; do
  dpkg-deb -x "$(fetch $p)" "$ST"
done

echo "== assemble $DST/lib (flat, dereferenced for native-Windows open()) =="
rm -rf "$DST"; mkdir -p "$DST/lib"
find "$ST" \( -path '*/lib/*' -o -path '*/usr/lib/*' \) -name '*.so*' -type f -exec cp -fL {} "$DST/lib/" \; 2>/dev/null || true
LD="$(find "$ST" -name 'ld-*.so' -type f | head -1)"
cp -fL "$LD" "$DST/lib/ld-linux.so.3"
# materialise every real lib's DT_SONAME as a real copy (Windows can't follow Linux symlinks)
cd "$DST/lib"
for f in *.so*; do
  [ -f "$f" ] || continue
  sn=$(readelf -d "$f" 2>/dev/null | awk '/SONAME/{gsub(/[][]/,"",$5);print $5}')
  [ -n "${sn:-}" ] && [ "$sn" != "$f" ] && cp -f "$f" "$sn" || true
done
# rg NEEDs libSDL_gfx.so.0 but Wheezy's soname is libSDL_gfx.so.13 (rotozoomSurface ABI is stable)
G=$(ls libSDL_gfx.so.13* 2>/dev/null | head -1); [ -n "${G:-}" ] && cp -f "$G" libSDL_gfx.so.0

echo "== fake-SDL shim + empty soname stubs (rg NEEDs these only to LOAD) =="
for n in libSDL-1.2.so.0 libSDL-1.2.so.0.11.2; do cp -f "$SH/libSDL-1.2.so.0" "$DST/lib/$n"; done
echo '' > "$W/empty.c"
for s in libSDL_image-1.2.so.0 libSDL_ttf-2.0.so.0 libjpeg.so.7 libpng12.so.0 \
         libfreetype.so.6 libvorbisidec.so.1; do
  $CC -shared -nostdlib -march=armv5te -marm -Wl,-soname,$s -o "$DST/lib/$s" "$W/empty.c"
done
# DRM gate stubs (harmless; lets DRM-locked EABI titles boot past the gate too)
for b in libinkadrm libdrmcode; do cp -f "$REPO/bin/guest/$b.so.0" "$DST/lib/$b.so.0" 2>/dev/null || true; done

echo "done. $(ls "$DST/lib" | wc -l) libs in $DST/lib"
echo "ME_GP2X_ROOTFS_EABI=$DST  (auto-found as assets/rootfs-eabi; selected by PT_INTERP)"
