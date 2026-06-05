# Handoff: Payback null-stream crash (blocks load → menu)

Native-threads rearchitecture is **done** (see NATIVE_THREADS.md). Payback now loads,
spawns real host threads, and renders — but the main thread crashes during load, stuck at
the loading screen. This doc is everything needed to crack that one crash in a fresh session.

## The crash (precise)
- Fault: `mem-fault type=19 (READ_UNMAPPED) @ 0x00000000  pc=0x0014ba4c  lr=0x0017c204`,
  main thread (tid 100), during the load-time mount/free-space check.
- `0x14b878` is glibc stdio **underflow** (vtable[4] of a `_IO_FILE`). It is **entered with a
  NULL FILE pointer**: a probe at `0x14b954` shows `r5=0, r0=0` from the start (r5=r0=the FILE).
  Null reads get lazily page-0-mapped, so it limps to `0x14ba4c: ldr r2,[r5]` (r5=0) and the
  later vtable call `0x14b95c: ldr pc,[r3,#0x38]` (r3=*(0+0x94)=junk) → `pc=0` → dead.
- A **VALID** `mntent` FILE exists in an outer frame: `r4=0x00f352d8`, `r4[0]=0xfbada488`
  (`_IO_MAGIC 0xFBAD` + filebuf flags), vtable `0x9d5cf0`, `__doallocate=*(vt+0x38)=0x14c9b0`
  (valid .text), read buffer mmap'd at `0x42800000`. **So stdio is NOT broken** — a *different*
  stream that the read path expects is null.
- Call chain at crash (innermost→outer): `0x14d828 → 0x14d6f0 → 0x149658 → 0x14970c →
  0x14aeb0 → 0x15a894`. `0x15a8xx` parses text checking for `#`/`\0` = glibc `__getmntent_r`.
  `0x14d814` = `sbumpc`-like (calls vtable[4]=underflow=`0x14b878` with r0=stream).
- `ME_NOMOUNTS=1` (makes `open("/proc/mounts")` return -ENOENT) → **crashes identically on a
  different file** (call chain `148e68/1340f8/1395c/146a90`). So it is a GENERAL "a stream that
  should be non-null is null" bug, not mounts-specific.

## Ruled out (with evidence) — don't re-investigate
- `fill_stat64` layout, `execve`/`system`, `read_cstr`, `_llseek`, exit_group — fixed, not this.
- `/proc/mounts` host table overrun — faked a minimal table; same crash.
- C++ `.ctors` not running — **they run** (probe confirmed the one ctor `0x131ce4` executes).
- Incomplete auxv / static-TLS — **replicated qemu's full create_elf_tables() auxv** (AT_PHDR/
  PHENT/PHNUM, AT_HWCAP 0x97, AT_ENTRY, AT_PLATFORM, etc. in elf.c); same crash.
- Unapplied relocations — `readelf -r` = **no relocations** (plain static ET_EXEC).
- glibc stdio init broken — **no**, the FILE is fully valid (see above).

## Recommended next step (most likely to crack it)
**Diff against the qemu backend — Payback works there.** Same binary, same glibc, qemu reaches
gameplay at 30fps. So the divergence is observable:
1. Run under qemu: `bash host/qemu/run-gp2x-qemu.sh ~/pbtest/Payback_tmp` (qemu source/tree at
   `~/src/qemu`, linux-user/).
2. In BOTH engines, log around the mount check: every `open()` path, the fd `setmntent`/`fopen`
   gets, and **which FILE* reaches the underflow**. Find the stream that's non-null under qemu
   but null under us, and trace what sets it.
Alternative: **single-step trace** — add a `UC_HOOK_CODE` over `0x14d6c0–0x14ba54` logging
pc + r0/r4/r5 for the last invocation before the fault, to follow the null FILE's origin
backward (which function loads/passes it — a global field? a malloc'd struct member that's 0?).

Hypothesis to probe: the null stream is likely a **secondary** glibc stream — e.g. a
`_IO_FILE._chain`, a backup/wide stream, or a per-thread `__getmntent_r` buffer/FILE — whose
pointer our engine leaves 0. Check what `0x149xxx`/`0x14d814`'s caller loads as the stream.

## Reproduce
```sh
# build (links the forked Unicorn at ~/me-unicorn-fork):
bash host/engine/build_engine.sh
# headless trace (paths + syscalls):
cd ~/pbtest && ME_TRACE=1 timeout 12 /mnt/e/Code/magiceyes/bin/me_unicorn ~/pbtest/Payback_tmp 2>/tmp/t.log
# interactive (viewer):  host/engine/run.sh ~/pbtest/Payback_tmp
```
Diagnostics already wired: `ME_TRACE` (syscalls + open/stat64 paths), `ME_THREADDUMP`,
`ME_NOMOUNTS`; `mem_invalid_cb` (mem.c) dumps regs + a **stack ret-addr call chain** + the
**FILE struct + vtable** on any null-pointer fault. Fork knobs: `ME_GP2X_SMCLOG`,
`ME_GP2X_NOSMCFREEZE`.

## Environment / gotchas
- Dev is **WSL Ubuntu** reached via `wsl.exe` from the MSYS Bash tool. MSYS **mangles inline
  `$VARS`/paths** in `wsl.exe bash -c '…'` — put logic in a **script file** and run
  `MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' wsl.exe bash /mnt/e/Code/magiceyes/<f>.sh`.
- Fork: `~/me-unicorn-fork` (Unicorn 2.0.1, ARM-only static, branch `magiceyes` w/ SMC-freeze),
  artifact `build/libunicorn.a`. Pushed to `github.com/zdiemer/unicorn`.
- Engine sources: `host/engine/{main,elf,mem,devices,syscalls,threads}.c` + `engine.h`.
- Disassembler: `arm-linux-gnueabi-objdump` (use `--start-address/--stop-address`).
- Test data: `~/pbtest/` (Payback_tmp static binary + `Data/` with the full music/maps).
- The binary: static `ET_EXEC` ARM, glibc 2.3.6, gcc 4.0.2; `.ctors` has one ctor `0x131ce4`.
