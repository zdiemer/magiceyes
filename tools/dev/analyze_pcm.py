#!/usr/bin/env python3
"""Analyze raw S16 PCM the shim produced. usage: analyze_pcm.py <raw> [rate] [channels]"""
import sys, array, math, wave

path = sys.argv[1]
rate = int(sys.argv[2]) if len(sys.argv) > 2 else 22050
ch = int(sys.argv[3]) if len(sys.argv) > 3 else 1
raw = open(path, "rb").read()
n = len(raw) // 2
a = array.array("h"); a.frombytes(raw[:n * 2])
if n == 0:
    print("EMPTY"); sys.exit(0)

frames = n // ch
print(f"bytes={len(raw)} samples={n} frames={frames} dur={frames/rate:.2f}s ch={ch} rate={rate}")

for c in range(ch):
    s = a[c::ch]
    m = len(s)
    rms = math.sqrt(sum(x * x for x in s) / m)
    zeros = sum(1 for x in s if x == 0)
    flips = sum(1 for i in range(1, m) if (s[i] >= 0) != (s[i-1] >= 0))
    # near-Nyquist "buzz": fraction of adjacent samples that reverse direction
    rev = sum(1 for i in range(2, m) if (s[i]-s[i-1]) * (s[i-1]-s[i-2]) < 0)
    print(f" ch{c}: min={min(s)} max={max(s)} rms={rms:.0f} "
          f"zeros={100*zeros/m:.1f}% signflips={flips*rate/m:.0f}/s dir_reversals={100*rev/m:.0f}%")

# mid-stream window (skip first 20%)
start = (frames // 5) * ch
print("mid window (16 frames):")
for f in range(16):
    base = start + f * ch
    if base + ch <= n:
        print("  ", [a[base + c] for c in range(ch)])

w = wave.open(path + ".wav", "wb")
w.setnchannels(ch); w.setsampwidth(2); w.setframerate(rate)
w.writeframes(raw[:frames * ch * 2]); w.close()
print("wrote", path + ".wav")
