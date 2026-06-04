#!/bin/bash
# Build pthread_bench with the GPH SDK glibc-2.3.6 toolchain (real LinuxThreads),
# statically, so it runs under qemu with no rootfs. Mirrors guest/build_guest.sh's
# toolchain staging (32-bit x86 cc1 -> must run from ext4, not /mnt drvfs).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SDK="${MAGICEYES_SDK:?set MAGICEYES_SDK to the GPH SDK dir (has tools/)}"
WORK="${MAGICEYES_WORK:-$HOME/.magiceyes}"
mkdir -p "$WORK"

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

BLD="$WORK/bench"; mkdir -p "$BLD"
cp -f "$HERE/pthread_bench.c" "$BLD/"
CFLAGS="-O2 -Wall -static -B $CC1DIR/ -B $ASDIR/ -B $GCCLIB/ -B $SYSLIB/ \
        -L $GCCLIB -L $SYSLIB -isystem $GCCINC -isystem $SYSINC"

echo "building pthread_bench (static, LinuxThreads) ..."
$GCC $CFLAGS -o "$BLD/pthread_bench" "$BLD/pthread_bench.c" -lpthread
chmod +x "$BLD/pthread_bench"
echo "built -> $BLD/pthread_bench"
file "$BLD/pthread_bench"
