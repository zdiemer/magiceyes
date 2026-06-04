#!/usr/bin/env python3
"""Headless stand-in for the SDL viewer's audio consumer: bump viewer_heartbeat
and drain a_read toward a_write at (roughly) the real device rate, exactly like
the viewer's audio callback advances a_read by what's available. Lets us strace
the game under viewer-like conditions without the GUI. Usage: fake_consumer.py [secs]"""
import mmap, os, struct, sys, time

NAME = "/dev/shm/gp2x_fb"
ARING = 1 << 19


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 20
    fd = os.open(NAME, os.O_RDWR)
    m = mmap.mmap(fd, 0, prot=mmap.PROT_READ | mmap.PROT_WRITE)
    t0 = time.time()
    chunk_t = 0.005                      # 5ms callbacks
    while time.time() - t0 < secs:
        freq = struct.unpack_from("<I", m, 24)[0] or 44100
        ch = struct.unpack_from("<I", m, 32)[0] or 2
        bps = freq * ch * 2
        want = int(bps * chunk_t)        # bytes the "device" consumes per tick
        aw, ar = struct.unpack_from("<II", m, 40)
        avail = (aw - ar) & 0xffffffff
        n = min(avail, want)             # viewer advances a_read by what's available
        struct.pack_into("<I", m, 44, (ar + n) & 0xffffffff)
        hb = struct.unpack_from("<I", m, 48)[0]
        struct.pack_into("<I", m, 48, (hb + 1) & 0xffffffff)
        time.sleep(chunk_t)


if __name__ == "__main__":
    main()
