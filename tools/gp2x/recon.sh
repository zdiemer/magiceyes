#!/bin/bash
set -u
ROOT=/mnt/e/Code/magiceyes/assets/rootfs/0/rootfs
G=/mnt/e/Code/magiceyes/assets/ginge
BB="$ROOT/bin/busybox"

echo "### tooling presence ###"
for t in qemu-arm-static strings readelf file objdump; do printf "%s: " "$t"; command -v "$t" || echo MISSING; done

echo; echo "### qemu busybox echo (plain) ###"
qemu-arm-static -L "$ROOT" "$BB" echo QEMU_ECHO_OK; echo "rc=$?"

echo; echo "### qemu busybox uname ###"
qemu-arm-static -L "$ROOT" "$BB" uname -a; echo "rc=$?"

echo; echo "### ginge_dyn: file + readelf header ###"
file "$G/ginge_dyn"
readelf -hd "$G/ginge_dyn" 2>&1 | grep -iE "Type|Machine|Flags|NEEDED|SONAME" | head

echo; echo "### ginge_dyn ALL strings, filtered for IO/video hints ###"
strings -a "$G/ginge_dyn" | grep -iE "dev|fb|sdl|x11|mem|gp2x|pollux|mmsp|/tmp|warm|env|GINGE|display|video|screen" | sort -u | head -60

echo; echo "### ginge_prep strings (handler/exec hints) ###"
strings -a "$G/ginge_prep" | grep -iE "dev|fb|sdl|gpe|exec|sloader|dyn|GINGE|tmp" | sort -u | head -40

echo; echo "### GINGE libSDL: host video backend hints ###"
strings -a "$G/lib/libSDL-1.2.so.0" | grep -iE "dev/fb|x11|fbcon|directfb|dummy|VIDEODRIVER|pollux|gp2x|/dev/" | sort -u | head -40
