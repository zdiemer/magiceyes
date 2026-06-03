#!/usr/bin/env python3
"""Walk the Deicide .dat: fixed 140-byte header per entry
(filename cstr at start, size uint32 @+132, data @+140)."""
import sys, struct

path = sys.argv[1]
data = open(path, "rb").read()
n = len(data)
HDR = 140
pos = 0; count = 0; bad = 0; total_data = 0
samples = []
while pos + HDR <= n:
    z = data.index(b"\x00", pos)
    name = data[pos:z].decode("latin1", "replace")
    size = struct.unpack_from("<I", data, pos + 132)[0]
    size2 = struct.unpack_from("<I", data, pos + 136)[0]
    if size != size2:
        print(f"  !! size mismatch @{pos} {name!r}: {size} vs {size2}"); bad += 1
        if bad > 3: break
    dstart = pos + HDR
    if count < 6 or "snd" in name or "wav" in name.lower():
        if len(samples) < 30:
            samples.append((count, pos, name, size))
    total_data += size
    pos = dstart + size
    count += 1
    if pos + HDR <= n and not data[pos:pos+4] == b"dat/":
        print(f"  !! desync @entry {count}, next={data[pos:pos+8]!r} @{pos}"); bad += 1
        if bad > 3: break

for c, p, nm, s in samples:
    print(f"[{c:5}] @{p:9} {nm:38} {s}")
print(f"entries={count} ended@{pos}/{n} (diff {n-pos}) data_total={total_data} desyncs={bad}")
