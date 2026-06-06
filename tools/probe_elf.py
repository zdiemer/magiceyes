#!/usr/bin/env python3
"""Dump device-discriminating ELF fields for GP2X-family .gpe binaries."""
import struct, sys, os

def u16(b,o): return struct.unpack_from('<H',b,o)[0]
def u32(b,o): return struct.unpack_from('<I',b,o)[0]

EABI_VER = {0:'OABI/0', 0x01000000:'v1',0x02000000:'v2',0x03000000:'v3',0x04000000:'v4',0x05000000:'v5'}
OSABI = {0:'SysV',3:'Linux',97:'ARM-EABI',255:'Standalone'}

def analyze(path):
    with open(path,'rb') as f: b=f.read()
    if b[:4]!=b'\x7fELF': return ('NOT-ELF',)
    ei_class=b[4]; ei_data=b[5]; ei_osabi=b[7]; ei_abiver=b[8]
    e_type=u16(b,16); e_machine=u16(b,18)
    e_phoff=u32(b,28); e_flags=u32(b,36)
    e_phentsize=u16(b,42); e_phnum=u16(b,44)
    eabi = e_flags & 0xff000000
    softfloat = bool(e_flags & 0x200)   # EF_ARM_SOFT_FLOAT (old) / VFP bits vary
    hardfloat = bool(e_flags & 0x400)
    interp=None; needed=[]
    dyn_off=dyn_sz=0; dyn_va=0
    loads=[]
    for i in range(e_phnum):
        o=e_phoff+i*e_phentsize
        if o+32>len(b): break
        p_type=u32(b,o); p_offset=u32(b,o+4); p_vaddr=u32(b,o+8)
        p_filesz=u32(b,o+16)
        if p_type==3: # PT_INTERP
            interp=b[p_offset:p_offset+p_filesz].split(b'\0')[0].decode('latin1')
        elif p_type==2: # PT_DYNAMIC
            dyn_off=p_offset; dyn_sz=p_filesz; dyn_va=p_vaddr
        elif p_type==1: # PT_LOAD
            loads.append((p_vaddr,p_offset,p_filesz))
    # parse DT_NEEDED (need strtab); map vaddr->offset via loads
    def va2off(va):
        for v,off,fsz in loads:
            if v<=va<v+fsz: return off+(va-v)
        return None
    if dyn_off:
        strtab_va=0; needed_off=[]
        o=dyn_off
        while o+8<=dyn_off+dyn_sz:
            tag=u32(b,o); val=u32(b,o+8-4)
            tag=u32(b,o); val=u32(b,o+4)
            if tag==0: break
            if tag==5: strtab_va=val      # DT_STRTAB
            if tag==1: needed_off.append(val)  # DT_NEEDED offset into strtab
            o+=8
        so=va2off(strtab_va) if strtab_va else None
        if so is not None:
            for n in needed_off:
                s=b[so+n:b.index(b'\0',so+n)].decode('latin1')
                needed.append(s)
    # GPEComp detection: scan for uclpack magic after image
    gpecomp = b.find(b'\x00\xe9UCL\xff\x01\x1a')>=0
    return dict(cls=ei_class, data=ei_data, osabi=OSABI.get(ei_osabi,ei_osabi),
                abiver=ei_abiver, etype={2:'EXEC',3:'DYN'}.get(e_type,e_type),
                flags=hex(e_flags), eabi=EABI_VER.get(eabi,hex(eabi)),
                interp=interp, needed=needed, gpecomp=gpecomp)

if __name__=='__main__':
    for p in sys.argv[1:]:
        try: r=analyze(p)
        except Exception as e: r=('ERR',str(e))
        name=p.split('Roms/')[-1]
        print(f"### {name}")
        if isinstance(r,dict):
            print(f"   type={r['etype']} osabi={r['osabi']} eabi={r['eabi']} flags={r['flags']} gpecomp={r['gpecomp']}")
            print(f"   interp={r['interp']}")
            print(f"   needed={r['needed']}")
        else:
            print("  ",r)
