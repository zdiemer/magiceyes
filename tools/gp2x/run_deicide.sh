#!/bin/bash
# Run Deicide 3 under qemu-user with the REAL Wiz DRM libs + dummy SDL video/audio.
# Goal: see how far it gets (DRM init? SDL init? game loop? asset read?) without a display.
set -u
ROOT=/mnt/e/Code/romnas/tools/scratch/gp2x/rootfs/0/rootfs
GAME_DIR="/mnt/e/Deicide 3/deicide3_eng"

cd "$GAME_DIR" || exit 2
echo "CWD=$(pwd)"
echo "=== files here ==="; ls -la

echo; echo "=== run (dummy video/audio, 25s cap, strace summary of interesting calls) ==="
# -E sets guest env; force SDL to dummy so it doesn't need /dev/fb0 or /dev/dsp.
timeout 25 qemu-arm-static -L "$ROOT" \
  -E SDL_VIDEODRIVER=dummy \
  -E SDL_AUDIODRIVER=dummy \
  -E SDL_NOMOUSE=1 \
  -E HOME=/tmp \
  -E LD_LIBRARY_PATH=/lib:/usr/lib \
  ./d3return_en.gpe 2>&1 | head -120
rc=${PIPESTATUS[0]}
echo "=== qemu_exit=$rc (124=timeout/still-running) ==="
