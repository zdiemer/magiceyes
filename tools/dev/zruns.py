#!/usr/bin/env python3
"""Examine zero-run structure + a loud window of stereo S16 PCM."""
import sys, array
raw = open(sys.argv[1], "rb").read()
ch = int(sys.argv[2]) if len(sys.argv) > 2 else 2
a = array.array("h"); a.frombytes(raw[:len(raw)//2*2])
n = len(a)
for c in range(ch):
    s = a[c::ch]; m = len(s)
    # zero-run histogram
    runs = {}; cur = 0
    for x in s:
        if x == 0: cur += 1
        elif cur: runs[cur] = runs.get(cur, 0) + 1; cur = 0
    # bucket
    b1 = sum(v for k, v in runs.items() if k == 1)
    b2 = sum(v for k, v in runs.items() if 2 <= k <= 8)
    b3 = sum(v for k, v in runs.items() if 9 <= k <= 64)
    b4 = sum(v for k, v in runs.items() if k > 64)
    print(f"ch{c}: zero-runs len1={b1} len2-8={b2} len9-64={b3} len>64={b4}  maxrun={max(runs) if runs else 0}")
# loud window
peak = max(range(len(a)), key=lambda i: abs(a[i]))
pf = (peak // ch) * ch
print("window around peak (12 frames):")
for f in range(-2, 10):
    base = pf + f * ch
    if 0 <= base and base + ch <= n:
        print("  ", [a[base + c] for c in range(ch)])
