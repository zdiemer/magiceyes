#!/bin/bash
# Capture a LOUD Cave Story audio dump (real-rate headless consumer + Start taps).
set -u
BASE=/mnt/e/Code/romnas/tools/scratch/gp2x
ROOT="$BASE/rootfs/0/rootfs"
GDIR="$BASE/games/doukutsu/doukutsu"
RAW=/tmp/cave.raw
cd "$GDIR" || exit 2
for f in libSDL-1.2.so.0 libSDL-1.2.so.0.11.2; do [ -f "$f" ] && mv -f "$f" "$f.orig"; done
rm -f /dev/shm/gp2x_fb "$RAW"

qemu-arm-static -L "$ROOT" \
  -E LD_LIBRARY_PATH=/opt/shim:/lib:/usr/lib -E HOME=/tmp \
  -E FAKESDL_AUDIO_DUMP="$RAW" \
  ${FAKESDL_AUDIO_TEST:+-E FAKESDL_AUDIO_TEST=1} \
  ./doukutsu.gpe >/tmp/cave.log 2>&1 &
QPID=$!
python3 "$BASE/shim/drain.py" 18          # real-rate consumer + Start taps
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null

echo "=== OpenAudio ==="; grep OpenAudio /tmp/cave.log
echo "=== analyze (stereo) ==="
python3 "$BASE/shim/analyze_pcm.py" "$RAW" 22050 2
cp "$RAW.wav" "$BASE/shots/cave_audio.wav" 2>/dev/null && echo "WAV -> shots/cave_audio.wav"
