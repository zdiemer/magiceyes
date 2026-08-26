#!/usr/bin/env python3
"""Extract the Deicide d3return_en.dat container into <outdir>.

Fixed 140-byte header per entry: filename cstr, size uint32 @+132, raw data @+140. The archive is
plaintext (no DRM), but it MUST be unpacked or the game's audio is garbage.
"""
import sys, struct, os

HDR = 140
NAME_MAX = 132          # the name field ends where the size field begins


def parse_entries(data):
    """Yield (name, blob) for each entry in a .dat image. Names use forward slashes."""
    pos, n = 0, len(data)
    while pos + HDR <= n:
        z = data.find(b"\x00", pos, pos + NAME_MAX)
        if z < 0:
            break                                   # not a name field: stop rather than guess
        name = data[pos:z].decode("latin1").replace("\\", "/")
        size = struct.unpack_from("<I", data, pos + 132)[0]
        yield name, data[pos + HDR:pos + HDR + size]
        pos += HDR + size


def safe_join(outdir, name):
    """Resolve an archive entry under `outdir`, or None if it would escape it.

    Entry names come from the file, so a crafted or corrupt archive could otherwise walk out of
    the destination with '..' or an absolute path and overwrite anything the user can write.
    """
    root = os.path.abspath(outdir)
    full = os.path.abspath(os.path.join(root, name.lstrip("/")))
    if full != root and not full.startswith(root + os.sep):
        return None
    return full


def extract(path, out):
    data = open(path, "rb").read()
    cnt = nbytes = skipped = 0
    for name, blob in parse_entries(data):
        fp = safe_join(out, name)
        if fp is None:
            print("skipping entry outside the output directory: %r" % name, file=sys.stderr)
            skipped += 1
            continue
        os.makedirs(os.path.dirname(fp), exist_ok=True)
        with open(fp, "wb") as f:
            f.write(blob)
        cnt += 1
        nbytes += len(blob)
    return cnt, nbytes, skipped


def main(argv):
    if len(argv) < 3:
        print("usage: extract_dat.py <archive.dat> <outdir>", file=sys.stderr)
        return 2
    cnt, nbytes, skipped = extract(argv[1], argv[2])
    print(f"extracted {cnt} files ({nbytes} bytes) to {argv[2]}"
          + (f", skipped {skipped} unsafe" if skipped else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
