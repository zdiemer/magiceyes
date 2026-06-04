#!/usr/bin/env python3
"""Sample the gp2x_fb shm once a second: frame_seq delta (fps), audio ring fill,
and viewer heartbeat rate. Reveals whether fps drops correlate with the ring
filling (audio backpressure) or not. Usage: mon.py [seconds]"""
import mmap, os, struct, sys, time

NAME = "/dev/shm/gp2x_fb"
ARING = 1 << 19
# offsets: magic0 w4 h8 seq12 buttons16 quit20 afreq24 afmt28 ach32 aactive36
#          a_write40 a_read44 viewer_heartbeat48
def main():
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    fd = os.open(NAME, os.O_RDONLY)
    m = mmap.mmap(fd, 0, prot=mmap.PROT_READ)
    def rd():
        seq = struct.unpack_from("<I", m, 12)[0]
        aw, ar, hb = struct.unpack_from("<III", m, 40)
        flips = struct.unpack_from("<I", m, 52)[0]   # reserved[0] = real game flips
        return seq, aw, ar, hb, flips
    pseq, paw, par, phb, pfl = rd()
    print(f"{'t':>3} {'GAME_fps':>9} {'present':>8} {'ring_fill':>10} {'drain_Bps':>10}")
    for t in range(secs):
        time.sleep(1)
        seq, aw, ar, hb, fl = rd()
        fill = (aw - ar) & 0xffffffff
        drain = (ar - par) & 0xffffffff
        gfps = (fl - pfl) & 0xffffffff
        print(f"{t:>3} {gfps:>9} {seq-pseq:>8} {fill:>10} {drain:>10}")
        pseq, paw, par, phb, pfl = seq, aw, ar, hb, fl

if __name__ == "__main__":
    main()
