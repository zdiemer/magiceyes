#!/usr/bin/env python3
"""Scan GP2X-family binaries for device-discriminating embedded strings."""
import sys, re
TOKENS = [
    b"gp2xmenu", b"/usr/gp2x", b"/usr/pollux", b"pollux", b"Pollux", b"POLLUX",
    b"mmsp2", b"MMSP2", b"mmuhack", b"/dev/mmuhack",
    b"/dev/pollux", b"pollux_clock", b"isa1200", b"/dev/isa1200",
    b"/dev/fb0", b"/dev/fb1", b"/dev/dsp", b"/dev/sound", b"/dev/mixer",
    b"/dev/gpio", b"/dev/GPIO", b"/dev/touchscreen", b"/dev/ts", b"/dev/i2c",
    b"/mnt/nand", b"/mnt/sd", b"/mnt/tmp", b"caanoo", b"Caanoo", b"CAANOO",
    b"wiz", b"Wiz", b"WIZ", b"f200", b"F200", b"f100", b"F100",
    b"SetLcdMode", b"TvConfig", b"/usr/gp2x/gp2xmenu",
    b"ldr_run", b"gp2x", b"GP2X",
]
for p in sys.argv[1:]:
    with open(p,'rb') as f: b=f.read()
    name=p.split('Roms')[-1].split('wizprobe')[-1]
    hits=[t.decode() for t in TOKENS if t in b]
    print(f"{name}\n   {hits}")
