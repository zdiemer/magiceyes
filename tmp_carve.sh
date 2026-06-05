#!/bin/bash
set -eu
cd /mnt/e/Code/magiceyes/host/engine
A=212; B=443   # devices block
{ printf '%s\n' \
  '/* magiceyes Unicorn engine — GP2X/Wiz device model: /dev/{fb,mem,gpio,dsp,mixer},' \
  ' * MMSP2 registers, the shm framebuffer/audio bridge to the viewer, and present. */' \
  '#include "engine.h"' ''
  sed -n "${A},${B}p" main.c
} > devices.c
sed -i 's/^static //' devices.c
sed -i '/^enum { DEV_FB = 1/d; /^#define DEVFD_BASE/d; /^struct memmap { uint32_t phys, guest, len; };/d' devices.c
sed -i "${A},${B}d" main.c
echo "devices.c: $(wc -l < devices.c) lines; main.c now: $(wc -l < main.c) lines"
