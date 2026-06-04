#!/bin/bash
set -u
BASE=/mnt/e/Code/romnas/tools/scratch/gp2x
ROOT="$BASE/rootfs/0/rootfs"
GDIR="$BASE/games/doukutsu/doukutsu"
cd "$GDIR" || exit 2
echo "=== sound-related files present in game data ==="
find . -iname "*.org" -o -iname "*.pxt" -o -iname "*.pcm" -o -iname "*.wav" -o -iname "*.dat" 2>/dev/null | head -20
echo "=== data/ subdirs ==="; ls data 2>/dev/null | head -30
rm -f /dev/shm/gp2x_fb
echo "=== strace: sound file opens (8s) ==="
timeout 8 qemu-arm-static -strace -L "$ROOT" \
  -E LD_LIBRARY_PATH=/opt/shim:/lib:/usr/lib -E HOME=/tmp \
  ./doukutsu.gpe >/tmp/c.out 2>/tmp/c.strace
grep -E "open(at)?\(" /tmp/c.strace | grep -iE "org|pxt|wav|pcm|snd|sound|music|\.dat|cache" | head -40
echo "=== open failures (ENOENT) sample ==="
grep -E "open(at)?\(" /tmp/c.strace | grep -i "errno=2 " | grep -ivE "\.so|/lib/|/etc/|/usr/lib|/hd/|fast-mult|/proc/" | head -20
echo "=== game stdout (sound init) ==="
grep -iE "sound|org|pxt|audio|music|init|load|fail|error|cache" /tmp/c.out | head -20