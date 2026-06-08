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
   -Wl,-soname,libSDL-1.2.so.0 -o "$SH/libSDL-1.2.so.0" "$REPO/guest/src/fakesdl.c" -lrt
bad=$(readelf -W --dyn-syms "$SH/libSDL-1.2.so.0" | awk '$7=="UND"{print $8}' \
      | grep -E '__gettimeofday64|__nanosleep64|__dlopen|GLIBC_2\.(1[4-9]|[2-9][0-9])' || true)
[ -z "$bad" ] || { echo "shim has too-new refs: $bad"; exit 1; }

echo "== cross-compile the fake-GLES1.1/EGL software rasterizer (Caanoo GPU emu) =="
# Self-contained (defines its own Khronos types); needs libc + libm + librt only.
$CC -shared -fPIC -O2 -march=armv5te -marm -mfloat-abi=soft -fno-stack-protector -D_FORTIFY_SOURCE=0 \
   --sysroot="$SYS" -B "$SYS/usr/lib/$MA" -L "$SYS/usr/lib/$MA" -L "$SYS/lib/$MA" \
   -nostdinc -I "$REPO/guest/src" \
   -isystem "$GCCINC" -isystem "$SYS/usr/include/$MA" -isystem "$SYS/usr/include" \
   -Wl,-soname,libGLESv1_CM.so -o "$SH/libGLESv1_CM.so" "$REPO/guest/src/fakegles.c" -lm -lrt

echo "== extract runtime libs =="
ST="$W/stage"; mkdir -p "$ST"
#   core: libc/ld/libm/libpthread/libdl/librt, C++ runtime, zlib
#   SDL_mixer + its real OGG decoder chain (bgm is .ogg) + mod/flac/mad (NEEDED, so must load)
#   SDL_gfx (rotozoomSurface)
#   Caanoo GLES titles need REAL image/font/audio leaf libs (not stubs): Propis decodes PNG
#   assets (libpng12) + a TTF font (libfreetype); Rhythmos streams MP3 (libmad + libid3tag).
#   libts (tslib): Caanoo touchscreen lib — Propis NEEDs libts-0.0.so.0 (clean deps: libdl+libc).
for p in libc6 libgcc1 libstdc++6 zlib1g \
         libsdl-mixer1.2 libsdl-gfx1.2-4 \
         libvorbisfile3 libvorbis0a libogg0 libmikmod2 libmad0 libflac8 \
         libpng12-0 libfreetype6 libid3tag0 libts-0.0-0; do
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
cd "$REPO"

echo "== glibc gconv modules (iconv: dynamic-text encoding conversion) =="
# Propis/Rhythmos convert their stored text via glibc iconv() before qtype4 rasterization.
# iconv_open() reads the gconv-modules config + dlopens converter .so files from the LITERAL
# path /usr/lib/$MA/gconv/. Our flat rootfs put the .so under /lib (where iconv can't find them)
# and never staged the config, so iconv_open failed -> empty converted string -> blank text
# surfaces (the "empty text box": agent name/stats, story dialogue). Stage the real gconv dir.
GC="$ST/usr/lib/$MA/gconv"
if [ -d "$GC" ]; then
  mkdir -p "$DST/usr/lib/$MA/gconv"
  cp -fLr "$GC/." "$DST/usr/lib/$MA/gconv/" 2>/dev/null || true
  echo "   staged $(find "$DST/usr/lib/$MA/gconv" -type f | wc -l) gconv files (config + modules)"
else
  echo "   WARNING: no gconv dir in libc6 extraction ($GC) -- iconv text will stay blank"
fi

echo "== fake-SDL shim + empty soname stubs (titles NEED these only to LOAD) =="
for n in libSDL-1.2.so.0 libSDL-1.2.so.0.11.2; do cp -f "$SH/libSDL-1.2.so.0" "$DST/lib/$n"; done
echo '' > "$W/empty.c"
for s in libSDL_ttf-2.0.so.0 libjpeg.so.7 libvorbisidec.so.1; do
  $CC -shared -nostdlib -march=armv5te -marm -Wl,-soname,$s -o "$DST/lib/$s" "$W/empty.c"
done
# libSDL_image stub: our fakesdl shim provides IMG_Load (decoding PNG via libpng12's weak-bound
# entrypoints). Give this stub a DT_NEEDED on libpng12 so a title that uses IMG_Load but doesn't
# link libpng itself (Liar) still pulls libpng into the process -> the weak refs resolve.
$CC -shared -nostdlib -march=armv5te -marm -Wl,-soname,libSDL_image-1.2.so.0 \
    -Wl,--no-as-needed -o "$DST/lib/libSDL_image-1.2.so.0" "$W/empty.c" "$DST/lib/libpng12.so.0"

echo "== fake-GLES shim under every Caanoo GL/EGL soname + Pollux driver stubs =="
# Our rasterizer exports the full gl*/egl* set; install it under each soname the games NEED
# (Khronos names for Rhythmos; the *_lite/glport pair for Propis). First definition wins at
# link time, so the real Pollux GPU driver libs below are pure load-time stubs.
for n in libGLESv1_CM.so libOpenEGL.so libopengles_lite.so libglport.so.0; do
  cp -f "$SH/libGLESv1_CM.so" "$DST/lib/$n"
done
for s in libMesNativeOEM.so libDrv.so libmedia.so librec.so libunicodefont.so libinifile.so; do
  $CC -shared -nostdlib -march=armv5te -marm -Wl,-soname,$s -o "$DST/lib/$s" "$W/empty.c"
done
# DRM gate stubs (Inka "NED") — cross-compile EABI so ld-linux.so.3 accepts the OS ABI.
# (The GPH-toolchain copies in bin/guest carry the firmware's OABI OS-ABI byte, which the
# Debian EABI ld-linux.so.3 rejects with "ELF file OS ABI invalid" — hit by Caanoo DRM titles
# Propis/Rhythmos/Liar; Patissier had no DRM so this was never exercised.) One source exports
# the full symbol set; stage it as BOTH sonames.
for b in libinkadrm libdrmcode; do
  $CC -shared -fPIC -O2 -march=armv5te -marm -mfloat-abi=soft -nostdinc \
     -isystem "$GCCINC" -isystem "$SYS/usr/include/$MA" -isystem "$SYS/usr/include" \
     --sysroot="$SYS" -B "$SYS/usr/lib/$MA" -L "$SYS/usr/lib/$MA" -L "$SYS/lib/$MA" \
     -Wl,-soname,$b.so.0 -o "$DST/lib/$b.so.0" "$REPO/guest/src/drmstub.c"
done

# Caanoo firmware system assets (fonts, etc.): titles open absolute paths like
# /usr/gp2x/HYUni_GPH_B_V1.01.ttf (the handset's Korean TrueType font, used by the DGE/QType4
# font engine in Propis/Rhythmos). These live only in the firmware (yaffs2_rfs.img). If a
# dereferenced copy has been staged into assets/caanoo-ref (see host/win/extract_caanoo_fw.sh),
# overlay it into the rootfs so the absolute opens resolve.
# EABI_REDIST=1 builds the SHIPPABLE rootfs: stage NO firmware font (it is proprietary). Caanoo
# titles then run from this FOSS-only tree; their on-screen text stays blank until the user installs
# the Caanoo firmware (Firmware -> Install firmware), which the engine overlays at /usr/gp2x at run
# time (firmware.c / me_rootfs_resolve). We deliberately do NOT substitute a FOSS font here -- a
# wrong-metrics substitution is brittle; the engine prompts for the real firmware instead.
REF="$REPO/assets/caanoo-ref"
if [ -n "${EABI_REDIST:-}" ]; then
  echo "== EABI_REDIST: shippable rootfs, no firmware fonts staged (installed via firmware flow) =="
elif [ -d "$REF/usr" ]; then
  echo "== overlay Caanoo firmware assets (assets/caanoo-ref/usr -> rootfs; local dev, NOT redistributable) =="
  cp -a "$REF/usr/." "$DST/usr/" 2>/dev/null || { mkdir -p "$DST/usr"; cp -a "$REF/usr/." "$DST/usr/"; }
fi

echo "done. $(ls "$DST/lib" | wc -l) libs in $DST/lib"
echo "ME_GP2X_ROOTFS_EABI=$DST  (auto-found as assets/rootfs-eabi; selected by PT_INTERP)"
