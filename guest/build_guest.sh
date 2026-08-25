#!/bin/bash
# Build the OS-agnostic ARM guest libraries:
#   libSDL-1.2.so.0          (our fake SDL: video->shm, input<-shm, audio mixer)
#   libinkadrm.so.0 / libdrmcode.so.0   (DRM gate stubs)
#
# Requires the GPH SDK toolchain (gcc-4.0.2-glibc-2.3.6, 32-bit x86 -> needs i386
# multilib). Outputs to magiceyes/bin/guest/. If MAGICEYES_ROOTFS is set, also
# stages the libs into <rootfs>/opt/shim/ ready for the launcher.
#
# Env:
#   MAGICEYES_SDK    path to the GPH SDK dir (contains tools/ and DGE/)  [required]
#   MAGICEYES_WORK   scratch dir on a native FS (NOT drvfs)  [default ~/.magiceyes]
#   MAGICEYES_ROOTFS device rootfs to stage into (optional)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ME="$(cd "$HERE/.." && pwd)"
SDK="${MAGICEYES_SDK:?set MAGICEYES_SDK to the GPH SDK dir (has tools/ and DGE/)}"
WORK="${MAGICEYES_WORK:-$HOME/.magiceyes}"
OUT="$ME/bin/guest"
mkdir -p "$OUT" "$WORK"

# gcc 4.0.2 fails to vfork+exec cc1 across drvfs (/mnt/*), and the 32-bit cc1
# hits EOVERFLOW stat()ing drvfs inodes -> run the toolchain + build on ext4.
TC_SRC="$SDK/tools/gcc-4.0.2-glibc-2.3.6"
TC="$WORK/gph_tc/gcc-4.0.2-glibc-2.3.6/arm-linux"
if [ ! -x "$TC/bin/arm-linux-gcc" ]; then
  echo "staging toolchain to $WORK/gph_tc ..."
  mkdir -p "$WORK/gph_tc"; cp -r "$TC_SRC" "$WORK/gph_tc/"
fi
GCC="$TC/bin/arm-linux-gcc"
CC1DIR="$(dirname "$(find "$TC" -name cc1 -type f | head -1)")"
ASDIR="$(dirname "$(find "$TC" -name as -type f | head -1)")"
GCCINC="$(dirname "$(find "$TC" -name stddef.h -path '*include*' | head -1)")"
GCCLIB="$(dirname "$GCCINC")"
SYSINC="$TC/arm-linux/sys-include"
SYSLIB="$TC/arm-linux/lib"
export PATH="$TC/bin:$ASDIR:$PATH"
export COMPILER_PATH="$CC1DIR:$ASDIR"
export GCC_EXEC_PREFIX="$TC/libexec/gcc/"

CFLAGS="-shared -fPIC -O2 -Wall -B $CC1DIR/ -B $ASDIR/ -B $GCCLIB/ -B $SYSLIB/ \
        -L $GCCLIB -L $SYSLIB -isystem $GCCINC -isystem $SYSINC"

# stage sources + SDL headers on ext4 (drvfs EOVERFLOW)
BLD="$WORK/build"; mkdir -p "$BLD/inc"
cp -f "$HERE/src/"*.c "$HERE/src/gp2xshm.h" "$HERE/src/glcmd.h" "$BLD/"
cp -rf "$SDK/DGE/include/." "$BLD/inc/"

echo "building libSDL-1.2.so.0 ..."
# gnu99: the shim uses for-loop decls; _GNU_SOURCE: glibc 2.3.6 LinuxThreads hides
# PTHREAD_MUTEX_RECURSIVE / pthread_mutexattr_settype behind __USE_UNIX98
$GCC $CFLAGS -std=gnu99 -D_GNU_SOURCE -I "$BLD/inc/SDL" -I "$BLD/inc" -I "$BLD" \
  -Wl,-soname,libSDL-1.2.so.0 -o "$BLD/libSDL-1.2.so.0" "$BLD/fakesdl.c" -lrt -ldl -lpthread

# fake-GLES offload shim (OABI Caanoo titles -- e.g. the Deicide-pack Propis -- link the firmware's
# real Pollux GLES driver, which we don't emulate; stage_rootfs.sh overlays this over those sonames).
# Self-contained (own Khronos types); forwards each draw to the engine GL backend (glcmd.h syscalls).
echo "building libGLESv1_CM.so (fake-GLES offload) ..."
$GCC $CFLAGS -std=gnu99 -I "$BLD" -Wl,-soname,libGLESv1_CM.so -o "$BLD/libGLESv1_CM.so" "$BLD/fakegles.c" -lm -lrt

for soname in libinkadrm.so.0 libdrmcode.so.0; do
  echo "building $soname ..."
  $GCC $CFLAGS -Wl,-soname,"$soname" -o "$BLD/$soname" "$BLD/drmstub.c"
done

cp -f "$BLD/libSDL-1.2.so.0" "$BLD/libGLESv1_CM.so" "$BLD/libinkadrm.so.0" "$BLD/libdrmcode.so.0" "$OUT/"
echo "built -> $OUT/"
ls -la "$OUT"

if [ -n "${MAGICEYES_ROOTFS:-}" ]; then
  mkdir -p "$MAGICEYES_ROOTFS/opt/shim"
  cp -f "$OUT/"*.so.0 "$MAGICEYES_ROOTFS/opt/shim/"
  rm -rf "$MAGICEYES_ROOTFS/dev/shm" 2>/dev/null || true   # use host /dev/shm
  echo "staged into $MAGICEYES_ROOTFS/opt/shim/"
fi
