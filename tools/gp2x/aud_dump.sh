#!/bin/bash
# Capture the PCM a game produces (pre-playback) + report its audio params.
# usage: aud_dump.sh <game_dir> <gpe> [secs]
set -u
BASE=/mnt/e/Code/romnas/tools/scratch/gp2x
ROOT="$BASE/rootfs/0/rootfs"
GDIR="$1"; GPE="$2"; SECS="${3:-8}"
RAW=/tmp/aud.raw
cd "$GDIR" || exit 2
for f in libSDL-1.2.so.0 libSDL-1.2.so.0.11.2; do [ -f "$f" ] && mv -f "$f" "$f.orig"; done
rm -f /dev/shm/gp2x_fb "$RAW"

qemu-arm-static -L "$ROOT" \
  -E LD_LIBRARY_PATH=/opt/shim:/lib:/usr/lib -E HOME=/tmp \
  -E FAKESDL_AUDIO_DUMP="$RAW" \
  "./$GPE" >/tmp/g.log 2>&1 &
QPID=$!
sleep 1
timeout "$SECS" "$BASE/shim/out/viewer" 2 >/tmp/view.log 2>&1   # consumer drives production
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null

echo "=== OpenAudio / CVT params ==="; grep -E "OpenAudio|BuildAudioCVT" /tmp/g.log | sort | uniq -c
echo "=== raw dump ==="; ls -la "$RAW" 2>&1
echo "=== analyze ==="; python3 "$BASE/shim/analyze_pcm.py" "$RAW"
cp "$RAW.wav" "$BASE/shots/$(basename "$GPE" .gpe)_audio.wav" 2>/dev/null && \
  echo "copied WAV -> shots/$(basename "$GPE" .gpe)_audio.wav"
