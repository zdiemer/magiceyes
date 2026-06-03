#!/usr/bin/env python3
"""Probe the Deicide d3return_en.dat container layout."""
import sys, struct, re

path = sys.argv[1]
import os
sz = os.path.getsize(path)
print(f"file size: {sz} ({sz/1024/1024:.1f} MB)")
with open(path, "rb") as f:
    head = f.read(4096)
    f.seek(max(0, sz - 4096)); tail = f.read(4096)

def hexdump(b, n=256, base=0):
    for i in range(0, min(n, len(b)), 16):
        chunk = b[i:i+16]
        hexs = " ".join(f"{x:02x}" for x in chunk)
        asc = "".join(chr(x) if 32 <= x < 127 else "." for x in chunk)
        print(f"  {base+i:08x}  {hexs:<48}  {asc}")

print("=== first 256 bytes ===");  hexdump(head, 256, 0)
print("=== last 128 bytes ===");   hexdump(tail, 128, sz-4096)

# scan a 4MB window at the start for dat/ paths and RIFF
with open(path, "rb") as f:
    win = f.read(4 * 1024 * 1024)
dat_off = [m.start() for m in re.finditer(rb"dat/", win)][:10]
riff_off = [m.start() for m in re.finditer(rb"RIFF", win)][:10]
print("first 'dat/' offsets (in first 4MB):", dat_off)
print("first 'RIFF' offsets (in first 4MB):", riff_off)
# show context around first dat/ path
if dat_off:
    o = dat_off[0]
    print("context around first dat/:"); hexdump(win[o-32:o+64], 96, o-32)
