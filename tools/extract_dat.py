#!/usr/bin/env python3
"""Extract the Deicide d3return_en.dat container (fixed 140-byte header per
entry: filename cstr, size uint32 @+132, raw data @+140) into <outdir>."""
import sys, struct, os

path, out = sys.argv[1], sys.argv[2]
data = open(path, "rb").read()
n = len(data); HDR = 140; pos = 0; cnt = 0; nbytes = 0
while pos + HDR <= n:
    z = data.index(b"\x00", pos)
    name = data[pos:z].decode("latin1").replace("\\", "/")
    size = struct.unpack_from("<I", data, pos + 132)[0]
    blob = data[pos + HDR:pos + HDR + size]
    fp = os.path.join(out, name)
    os.makedirs(os.path.dirname(fp), exist_ok=True)
    with open(fp, "wb") as f:
        f.write(blob)
    pos += HDR + size; cnt += 1; nbytes += size
print(f"extracted {cnt} files ({nbytes} bytes) to {out}")
