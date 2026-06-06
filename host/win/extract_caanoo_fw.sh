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
sudo chmod -R a+rX "$REPO/assets/caanoo-ref"
echo "done: staged $n font(s) into $DST"
ls -la "$DST"
