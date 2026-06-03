#!/usr/bin/env python3
"""Render one raw capture several ways so the operator can identify the true
layout by ear. Writes into shots/."""
import sys, array, wave, os

raw = open(sys.argv[1], "rb").read()
out = sys.argv[2]
a = array.array("h"); a.frombytes(raw[:len(raw)//2*2])

def save(name, data, ch, rate):
    w = wave.open(os.path.join(out, name), "wb")
    w.setnchannels(ch); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(data.tobytes() if isinstance(data, array.array) else data)
    w.close(); print("wrote", name)

save("cv_stereo_22k.wav", a, 2, 22050)            # as-is (what we assume)
save("cv_monoL_22k.wav", a[0::2], 1, 22050)       # left channel only
save("cv_monoR_22k.wav", a[1::2], 1, 22050)       # right channel only
save("cv_mono_44k.wav", a, 1, 44100)              # all samples as 1ch @44.1k
# byte-swapped (endian) stereo
b = bytearray(raw[:len(raw)//2*2])
b[0::2], b[1::2] = b[1::2], b[0::2]
save("cv_stereo_swap.wav", bytes(b), 2, 22050)
