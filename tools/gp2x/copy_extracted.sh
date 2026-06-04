#!/bin/bash
set -u
SRC="/mnt/e/Deicide 3/deicide3_eng/dat/snd"
DST="/mnt/e/Code/magiceyes/assets/shots/extracted"
mkdir -p "$DST"
cp "$SRC/eff/wav00.wav" "$SRC/eff/wav01.wav" "$SRC/eff/cta.wav" \
   "$SRC/eff/tick.wav" "$SRC/voice/000002.wav" "$DST/" 2>&1
echo "=== copied to shots/extracted/ ==="
ls -la "$DST"
echo "=== formats ==="
python3 - "$DST" <<'PY'
import struct, sys, glob, os
for f in sorted(glob.glob(os.path.join(sys.argv[1], "*.wav"))):
    d = open(f, "rb").read(80)
    i = d.find(b"fmt ")
    af, ch, sr, br, ba, bits = struct.unpack_from("<HHIIHH", d, i + 8)
    data_i = d.find(b"data")
    print(f"{os.path.basename(f):24} fmt={af} ch={ch} rate={sr} bits={bits}")
PY
echo "=== also: what rates does the game's BuildAudioCVT see? (from last run) ==="
grep "BuildAudioCVT" /tmp/g.log 2>/dev/null | sort | uniq -c
