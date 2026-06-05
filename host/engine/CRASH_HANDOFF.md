# RESOLVED: Payback null-stream load crash

**Status: fixed (2026-06-05).** Payback now boots past the load-time crash into its main
multi-threaded frame loop under the native-threads Unicorn engine.

## Root cause (NOT a null stream)
The fault presented as `_IO_file_underflow` (`0x14b878`) dereferencing a **null `_IO_FILE`**
during the first `getmntent` read — but the FILE was valid on entry. The real bug was a
**stack buffer overflow in `fill_stat64`**:

- This GP2X glibc 2.3.6 is **OABI** (`long long` is 4-byte aligned), so its `struct stat64`
  is **packed to 96 bytes** — NOT the 104-byte EABI struct we were writing.
- Proven from `_IO_file_doallocate` (`0x17c168`): `push {r4,r5,r6,lr}; sub sp,#104;
  add r1,sp,#8` puts `struct stat64` at `sp+8`, and it reads `st_blksize` at `[sp,#60]` =
  struct **+52** → the struct is exactly the 96 bytes `sp+8..sp+104`.
- Our `fstat64` wrote **104** bytes to `sp+8`, so the trailing 64-bit `st_ino` (b+96) landed
  at `sp+104`/`sp+108` — the saved `r4`/`r5`. Inodes are 32-bit, so the high word is 0 →
  saved `r5` (the `FILE*`) became 0. `_IO_file_doallocate` then `pop {r4,r5,r6,pc}` returns a
  null `r5`, and `_IO_file_underflow` derefs it → crash at `0x14ba4c`.

## The fix
`host/engine/syscalls.c` `fill_stat64`: write the **96-byte OABI** layout —
`st_mode@16, st_rdev@32(8), st_size@44(8, 4-aligned), st_blksize@52, st_blocks@56(8),
st_ino@88(8), sizeof 96`. No other change was needed.

## How it was found
Hooked the `__getmntent_r → fgets → __uflow → underflow` chain (a temporary `UC_HOOK_CODE`
trace) and logged `r5` across the call: `underflow` *entered* with `r5=0x00f352d8` (valid),
and `r5` was preserved across each syscall, but `_IO_file_doallocate` *returned* with `r5=0`.
That isolated the clobber to a stack-save overwrite, and the disassembly of `0x17c168`
confirmed the 96-vs-104 struct overflow. (The original handoff's "null secondary stream"
hypothesis was wrong.)

## Verification
`cd ~/pbtest && ME_TRACE=1 timeout 20 .../bin/me_unicorn ~/pbtest/Payback_tmp` → 0 mem-faults;
game opens `/etc/localtime`, stat's all `Data/Maps/*`, spawns tids 101–103, and runs a steady
`nanosleep`/`poll` frame loop. Menu validation is interactive (needs the viewer + input).

## Known follow-ups (separate from this crash)
- Unicorn TCOUNT (`host/engine/devices.c`) ticks at **1 MHz** wall-clock (slow-motion) vs the
  hardware-correct **7.3728 MHz** the qemu backend uses for 30fps. Scale it for playable speed.
- Port `gp2x.c`'s MLC-palette/blitter MMIO trap and `/dev/i2c-0` serial for other titles.
