#!/bin/bash
# Assemble a LeapFrog Didj (LF1000) rootfs for the native engine's dynamic-ELF path.
#
# A Didj game is a dynamically-linked ARM EABI shared object (App.so) launched against
# uClibc 0.9.29 (interpreter /lib/ld-uClibc.so.0) plus the LeapFrog "MPI" HAL libraries
# (libDisplayMPI/libAudioMPI/libButtonMPI/libKernelMPI/... + libLightningBase). Those live
# in two operator-supplied places:
#   - the uClibc BASE (libc/ld.so/libm/libpthread/libdl + busybox) -> erootfs.jffs2
#   - the MPI/Lightning RUNTIME -> the "Didj OS" FAT16 image (didj-roms/Didj OS), Base/ tree
#
# This stages the uClibc base from erootfs.jffs2 (Phase B: enough to boot a uClibc binary),
# then overlays the MPI/Lightning libs from the Didj OS image when DIDJ_OS_IMG is given
# (Phase C/D). The result is gitignored (assets/), like the other device rootfses.
#
# Env:
#   DIDJ_EROOTFS   path to erootfs.jffs2            (default: F:\Roms\Didj\erootfs\erootfs.jffs2)
#   DIDJ_OS_IMG    path to the "Didj OS" FAT16 image (optional; overlays MPI/Lightning libs)
#   JEFFERSON      jefferson binary (default: ~/.didj-tools/bin/jefferson)
#   OUT            output rootfs dir               (default: <repo>/assets/rootfs-didj)
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
EROOTFS="${DIDJ_EROOTFS:-/mnt/f/Roms/Didj/erootfs/erootfs.jffs2}"
JEFFERSON="${JEFFERSON:-$HOME/.didj-tools/bin/jefferson}"
OUT="${OUT:-$REPO/assets/rootfs-didj}"

[ -f "$EROOTFS" ] || { echo "erootfs.jffs2 not found at $EROOTFS (set DIDJ_EROOTFS)"; exit 1; }
command -v "$JEFFERSON" >/dev/null 2>&1 || [ -x "$JEFFERSON" ] || {
    echo "jefferson not found ($JEFFERSON). Create it: python3 -m venv ~/.didj-tools && \\"
    echo "  ~/.didj-tools/bin/pip install jefferson"; exit 1; }

echo "[didj] extracting uClibc base from $EROOTFS"
TMP="$(mktemp -d)/eroot"; trap 'rm -rf "$(dirname "$TMP")"' EXIT
"$JEFFERSON" -d "$TMP" "$EROOTFS" >/dev/null 2>&1 || true
# Locate the fs root by finding the interpreter, wherever jefferson placed it (it may nest under
# a numbered/fs subdir). SRC = the dir whose lib/ holds ld-uClibc.so.0.
LD="$(find "$TMP" -path '*/lib/ld-uClibc.so.0' 2>/dev/null | head -1)"
[ -n "$LD" ] || { echo "[didj] ERROR: no lib/ld-uClibc.so.0 in the extracted tree"; \
                  find "$TMP" -maxdepth 3 -name 'ld-uClibc*' 2>/dev/null | head; exit 1; }
SRC="$(dirname "$(dirname "$LD")")"

rm -rf "$OUT"; mkdir -p "$OUT"
cp -a "$SRC"/. "$OUT"/
echo "[didj] uClibc base staged -> $OUT"
ls "$OUT/lib/" | grep -E 'ld-uClibc|libc\.so|libm\.so|libpthread|libdl' || true

# Optional: overlay the Didj OS runtime from the "Didj OS" FAT16 image (Phase C/D).
# The real device mounts this image at /Didj, runs /Didj/Base/bin/RealAppManager (the base
# UI + game launcher) with LD_LIBRARY_PATH=/Didj/Base/Brio/lib:/Didj/Base/lib. We mirror that:
# stage the whole Base/ tree at <rootfs>/Didj/Base (RealAppManager opens fonts/UI/settings by
# absolute /Didj path), and ALSO copy every .so into <rootfs>/lib so ld.so finds them via the
# default /lib:/usr/lib search (no LD_LIBRARY_PATH change needed). assets/ is gitignored.
DIDJ_OS_IMG="${DIDJ_OS_IMG:-/mnt/f/Roms/Didj/didj-roms/Didj OS}"
if [ -f "$DIDJ_OS_IMG" ]; then
    echo "[didj] staging Didj OS runtime from $DIDJ_OS_IMG"
    MNT="$(mktemp -d)"
    if sudo mount -o loop,ro "$DIDJ_OS_IMG" "$MNT" 2>/dev/null; then
        mkdir -p "$OUT/Didj"
        cp -a "$MNT/Base" "$OUT/Didj/" 2>/dev/null || true
        [ -d "$MNT/Data" ] && cp -a "$MNT/Data" "$OUT/Didj/" 2>/dev/null || true
        # Mirror every shared object into /lib so the default linker search resolves them.
        find "$OUT/Didj" \( -name '*.so' -o -name '*.so.*' \) 2>/dev/null | while read -r so; do
            cp -a "$so" "$OUT/lib/" 2>/dev/null || true
        done
        sudo umount "$MNT"
        echo "[didj] Didj OS runtime staged -> $OUT/Didj/Base ; libs mirrored into $OUT/lib"
        ls "$OUT/lib" | grep -cE 'MPI|Lightning' | xargs -I{} echo "[didj]   {} MPI/Lightning libs in /lib"
    else
        echo "[didj] WARN: could not loop-mount $DIDJ_OS_IMG (need root); skipping runtime overlay"
    fi
    rmdir "$MNT" 2>/dev/null || true
else
    echo "[didj] (no Didj OS image at $DIDJ_OS_IMG -> uClibc base only; set DIDJ_OS_IMG for the runtime)"
fi

echo "[didj] done. Run: ME_GP2X_ROOTFS_DIDJ=$OUT MAGICEYES_DEVICE=didj \\"
echo "         bin/me_unicorn $OUT/Didj/Base/bin/RealAppManager"
