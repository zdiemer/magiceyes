#!/bin/bash
# Extract the Caanoo firmware rootfs (YAFFS2) and stage the system assets our titles open by
# absolute path (the /usr/gp2x Korean TrueType fonts the DGE/QType4 engine in Propis/Rhythmos
# needs) into assets/caanoo-ref/. stage_rootfs_eabi.sh then overlays assets/caanoo-ref/usr into
# the EABI rootfs so e.g. open("/usr/gp2x/HYUni_GPH_B_V1.01.ttf") resolves.
#
# The Caanoo firmware (v1.6.0_caanoo.zip) ships the rootfs as yaffs2_rfs.img (NAND n35:
# 2048-byte page + 64-byte OOB, == default unyaffs geometry). UNlike the Wiz UBIFS, this is
# YAFFS2. unyaffs must run as root (it recreates device nodes / mode-000 files).
#
# Run from WSL/Linux:  bash host/win/extract_caanoo_fw.sh [path/to/v1.6.0_caanoo.zip]
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
ZIP="${1:-${MAGICEYES_CAANOO_FW:-}}"
[ -n "$ZIP" ] && [ -f "$ZIP" ] || { echo "usage: $0 <v1.6.0_caanoo.zip>  (or set MAGICEYES_CAANOO_FW)"; exit 1; }
command -v unyaffs >/dev/null || { echo "need unyaffs (sudo apt install unyaffs)"; exit 1; }

W="$(mktemp -d)"; trap 'sudo rm -rf "$W"' EXIT
echo "== extract yaffs2_rfs.img from $ZIP =="
if command -v 7z >/dev/null;     then 7z e "$ZIP" yaffs2_rfs.img -o"$W" -y >/dev/null
elif command -v unzip >/dev/null;then unzip -o "$ZIP" yaffs2_rfs.img -d "$W" >/dev/null
else echo "need 7z or unzip"; exit 1; fi
[ -f "$W/yaffs2_rfs.img" ] || { echo "yaffs2_rfs.img not found in zip"; exit 1; }

echo "== unyaffs (as root) =="
sudo rm -rf "$W/rfs"; mkdir -p "$W/rfs"
sudo unyaffs "$W/yaffs2_rfs.img" "$W/rfs" >/dev/null 2>&1 || true   # exits nonzero on device nodes; assets still extracted

echo "== stage /usr/gp2x fonts -> assets/caanoo-ref =="
DST="$REPO/assets/caanoo-ref/usr/gp2x"; mkdir -p "$DST"
n=0
for f in "$W/rfs/usr/gp2x/"*.ttf; do
  [ -e "$f" ] || continue
  sudo cp -Lf "$f" "$DST/$(basename "$f")"; n=$((n+1))
done
echo "== stage GPH runtime libs (/usr/lib + /lib) -> assets/caanoo-ref/usr/lib =="
# DGE titles NEED the firmware's own engine libs (deminor: libdge20.so). These only exist in
# the firmware image. Some carry the GPH toolchain's ARM OS-ABI byte, which the Debian EABI
# ld-linux.so.3 rejects ("ELF file OS ABI invalid") -- normalise EI_OSABI/EI_ABIVERSION to
# SYSV/0 while staging (the code is plain EABI5; only the branding byte differs).
LDST="$REPO/assets/caanoo-ref/usr/lib"; mkdir -p "$LDST"
m=0
# WHITELIST, not a sweep: everything else is either Wheezy-staged (SDL satellites, decoders),
# shimmed on purpose (libSDL, GLES, Pollux driver stubs), or firmware glibc that must never mix
# with the Wheezy glibc in the link map.
for pat in libdg libopenal; do  # DGE family (libdge20/dgx20/dgt20) + GPH OpenAL (libopenal11/alut11)
  for f in "$W/rfs/usr/lib/$pat"*.so* "$W/rfs/lib/$pat"*.so*; do
    [ -e "$f" ] || continue
    b=$(basename "$f")
    sudo cp -Lf "$f" "$LDST/$b"
    sudo chmod 644 "$LDST/$b"
    printf '\0\0' | sudo dd of="$LDST/$b" bs=1 seek=7 count=2 conv=notrunc status=none
    m=$((m+1))
  done
done
sudo chmod -R a+rX "$REPO/assets/caanoo-ref"
echo "done: staged $n font(s) into $DST, $m runtime lib(s) into $LDST"
ls -la "$DST"
