#!/usr/bin/env python3
"""Replicate the C load_elf device heuristic over real ROMs; compare to the source folder."""
import sys, struct
CAANOO_SIG=[b"libopengles_lite",b"libGLESv1_CM",b"libOpenEGL",b"libglport",b"libMesNativeOEM",
  b"libDrv.so",b"libmedia.so",b"librec.so",b"libdge20.so",b"libdgt20.so",b"libdgx20.so",
  b"/dev/pollux",b"pollux_clock",b"/dev/isa1200"]
WIZ_SIG=[b"libtngp2xtk.so",b"SDL_SetLcdMode",b"SetLcdMode",b"SDL_TvConfig"]
def get_interp(b):
    if b[:4]!=b'\x7fELF': return None
    e_phoff=struct.unpack_from('<I',b,28)[0]; e_phentsize=struct.unpack_from('<H',b,42)[0]
    e_phnum=struct.unpack_from('<H',b,44)[0]
    for i in range(e_phnum):
        o=e_phoff+i*e_phentsize
        if struct.unpack_from('<I',b,o)[0]==3:
            off=struct.unpack_from('<I',b,o+4)[0]; fsz=struct.unpack_from('<I',b,o+16)[0]
            return b[off:off+fsz].split(b'\0')[0].decode('latin1')
    return ""   # static (no PT_INTERP)
def classify(path,b):
    interp=get_interp(b)
    if interp is None: return "NOT-ELF"
    if not interp: return "GP2X"   # static
    if any(s in b for s in CAANOO_SIG): return "Caanoo"
    if (b"pollux" in b) or (b"Pollux" in b): return "Caanoo"
    if "_Pollux" in path or "_pollux" in path: return "Caanoo"
    if any(s in b for s in WIZ_SIG): return "Wiz"
    if "ld-linux.so.3" in interp: return "Caanoo"   # so.3 = never GP2X; default Caanoo
    return "GP2X"
def folder(path):
    if "Caanoo" in path: return "Caanoo"
    if "Wiz" in path or "wizprobe" in path: return "Wiz"
    return "GP2X"
for p in sys.argv[1:]:
    with open(p,'rb') as f: b=f.read()
    c=classify(p,b); fo=folder(p)
    name=p.split('Roms')[-1].split('wizprobe')[-1]
    mark="" if c==fo or c=="NOT-ELF" else "  <<< DIFFERS from folder %s"%fo
    print(f"{c:8} {name}{mark}")
