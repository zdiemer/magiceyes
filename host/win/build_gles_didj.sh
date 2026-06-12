#!/bin/bash
# Build the Didj GLES shim: our fakegles.c (software GLES1.1/EGL that forwards draws to the
# engine's GL offload) as a drop-in replacement for the LeapFrog libopengles_lite.so. The real
# lib drives the LF1000 GA3D 3D GPU at the register level (which we don't emulate); ours renders
# in software/host-GL and presents to the shm. Staged over the real soname in assets/rootfs-didj.
#
# Built to load under the Didj's ld-uClibc.so.0:
#   - -nostdlib + link the rootfs uClibc libc/libm so it NEEDs libc.so.0 (not glibc's libc.so.6);
#   - --hash-style=both: uClibc 0.9.29 only reads the SysV .hash section (modern ld emits GNU hash);
#   - legacy 32-bit time/LFS ABI: Ubuntu's cross-gcc defaults to time64/LFS (__gettimeofday64,
#     mmap64, ...) which uClibc 0.9.29 lacks;
#   - armv5te soft-float to match the ARM926/uClibc ABI;
#   - a sincosf shim (gcc folds sinf/cosf into it; uClibc 0.9.29 has no sincosf).
#
# Env: GCC (default arm-linux-gnueabi-gcc), RFS (default <repo>/assets/rootfs-didj).
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
GCC="${GCC:-arm-linux-gnueabi-gcc}"
RFS="${RFS:-$REPO/assets/rootfs-didj}"
OUT="$RFS/lib/libopengles_lite.so"
[ -f "$RFS/lib/libc.so.0" ] || { echo "no uClibc rootfs at $RFS (run stage_rootfs_didj.sh first)"; exit 1; }
command -v "$GCC" >/dev/null || { echo "$GCC not found"; exit 1; }
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

ARCH="-march=armv5te -mfloat-abi=soft -fPIC -O2 -std=gnu99 -fno-stack-protector -U_FORTIFY_SOURCE \
  -D_FILE_OFFSET_BITS=32 -D_TIME_BITS=32 -fno-builtin-sincosf -fno-builtin-sincos"

echo "[gles-didj] compiling fakegles.c"
$GCC $ARCH -I "$REPO/guest/src" -c "$REPO/guest/src/fakegles.c" -o "$TMP/fakegles.o"
printf '#include <math.h>\nvoid sincosf(float x,float*s,float*c){*s=sinf(x);*c=cosf(x);}\n' > "$TMP/compat.c"
$GCC $ARCH -fno-builtin -c "$TMP/compat.c" -o "$TMP/compat.o"

echo "[gles-didj] linking libopengles_lite.so (uClibc, SysV+GNU hash)"
$GCC -nostdlib -shared -Wl,-soname,libopengles_lite.so -Wl,--hash-style=both \
  "$($GCC -print-file-name=crti.o)" "$($GCC -print-file-name=crtbeginS.o)" \
  "$TMP/fakegles.o" "$TMP/compat.o" \
  "$RFS/lib/libc.so.0" "$RFS/lib/libm.so.0" "$RFS/lib/librt.so.0" \
  "$($GCC -print-libgcc-file-name)" \
  "$($GCC -print-file-name=crtendS.o)" "$($GCC -print-file-name=crtn.o)" \
  -o "$OUT"
echo "[gles-didj] built $OUT"
readelf -d "$OUT" | grep NEEDED | grep -q 'libc.so.0' && echo "[gles-didj] OK: NEEDs uClibc libc.so.0" \
  || echo "[gles-didj] WARN: unexpected NEEDED (should be libc.so.0)"
