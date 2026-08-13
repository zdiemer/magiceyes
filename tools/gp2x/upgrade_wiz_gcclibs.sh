#!/bin/bash
# Upgrade the staged Wiz/OABI rootfs's libgcc_s to the gcc-4.2-era build the Wiz homebrew
# scene shipped beside their games.
#
# Why: the extracted Wiz firmware carries libgcc_s topping out at GCC_4.0.0, but a cluster of
# Wiz titles (openggs, smw_1.7, supertux-wiz, roadfighter, wolf4sdl, abuse, openjazz,
# kuklomenos...) was built with the later community toolchain and needs GCC_4.2.0; ld.so
# refuses the version check -> exit-1 before main. Those same titles bundle the newer libgcc,
# all identical builds against this exact glibc 2.3.6 (needs <= GLIBC_2.2.4), so promoting one
# copy into the rootfs fixes the whole cluster. (Reordering LD_LIBRARY_PATH game-dir-first was
# tried instead and broke titles that need the rootfs SDL family to win.)
#
# libstdc++ is deliberately NOT upgraded: the matching 6.0.9 throws
# __gnu_cxx::__concurrence_lock_error in its gthread path under the engine's LinuxThreads
# emulation (regressed BareFistFighter when tried on 2026-08-13). The few titles needing
# GLIBCXX_3.4.5+ (alephone, epiphany, prboom, cgenius) fail the same way with their own
# bundled copies, so they are engine-blocked either way.
#
# libgcc is a GPL-with-runtime-exception gcc runtime library: redistributable.
#
# Usage: bash tools/gp2x/upgrade_wiz_gcclibs.sh [<rootfs-lib-dir>]
set -eu

LIBDIR=${1:-$(cd "$(dirname "$0")/../.." && pwd)/assets/rootfs/0/rootfs/lib}
SRC_GCC="/mnt/s/GP2X Wiz/kuklomenos/kuklomenos/libgcc_s.so.1"

[ -d "$LIBDIR" ] || { echo "no such lib dir: $LIBDIR"; exit 1; }
[ -f "$SRC_GCC" ] || { echo "corpus source missing: $SRC_GCC (is /mnt/s mounted?)"; exit 1; }
strings -a "$SRC_GCC" | grep -qx 'GCC_4.2.0' || { echo "donor libgcc lacks GCC_4.2.0"; exit 1; }

dst="$LIBDIR/libgcc_s.so.1"
# Never write through a symlink (the firmware rootfs links soname -> real file; cp -f would
# clobber the real file and a cp -a "backup" of the symlink backs up nothing).
if [ -L "$dst" ]; then real=$(readlink -f "$dst"); rm -f "$dst"; cp -f "$real" "$dst"; fi
if [ -f "$dst" ] && [ ! -f "$dst.fw-orig" ]; then cp -fL "$dst" "$dst.fw-orig"; fi
cp -fL "$SRC_GCC" "$dst"
chmod 755 "$dst"
echo "upgraded $dst ($(strings -a "$dst" | grep -E '^GCC_[0-9.]+$' | sort -V | tail -1))"
echo "done. re-run any rootfs staging (run_nas_sweep.sh restages automatically)."
