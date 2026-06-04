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
        return seq, aw, ar, hb
    pseq, paw, par, phb = rd()
    print(f"{'t':>3} {'fps':>5} {'ring_fill':>10} {'drain_Bps':>10} {'hb/s':>6}")
    for t in range(secs):
        time.sleep(1)
        seq, aw, ar, hb = rd()
        fill = (aw - ar) & 0xffffffff
        drain = (ar - par) & 0xffffffff
        print(f"{t:>3} {seq-pseq:>5} {fill:>10} {drain:>10} {hb-phb:>6}")
        pseq, paw, par, phb = seq, aw, ar, hb

if __name__ == "__main__":
    main()
