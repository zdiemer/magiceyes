# Unicorn fork patches (cross-platform engine)

The cross-platform magiceyes engine runs on a **fork of Unicorn 2.0.1** (= qemu's TCG
JIT as a portable library, builds native on Windows/macOS/Linux). The fork carries the
hard-won GP2X fixes we originally made in the qemu-user backend (retired in 0.5.0),
ported to Unicorn's **softmmu** internals.

The fork itself is a git clone with our patches as **real commits** on a `magiceyes`
branch — the fork is the source of truth. The `*.py` scripts here are the reproducible
changelog / authoring tool (they edit the fork's qemu sources; `apply_and_build.sh` then
commits and builds). This will be formalized as a `third_party/unicorn` git submodule in
Phase 1 (CMake); for now the fork lives on ext4 for fast WSL builds.

## Setup (WSL/Linux, one-time)
```sh
git clone --depth 1 --branch 2.0.1 https://github.com/unicorn-engine/unicorn.git ~/me-unicorn-fork
cd ~/me-unicorn-fork && mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DUNICORN_ARCH=arm -DBUILD_SHARED_LIBS=OFF && ninja
```

## Apply patches + build + relink the engine
```sh
host/engine/fork-patches/apply_and_build.sh        # ME_UNICORN_FORK overrides ~/me-unicorn-fork
```

## Patches
- **`smc_freeze.py`** — GP2X SMC-freeze in `qemu/accel/tcg/cputlb.c`. GP2X games co-locate
  hot `.iwram` scratch *data* with hot *code* on one page, so every data store triggers a
  false self-modifying-code TB invalidation (`notdirty_write` → `tb_invalidate_phys_page_fast`).
  Once a 4KB span thrashes past 512 faults we stop SMC-protecting it (skip the invalidation
  and stop re-arming `TLB_NOTDIRTY`). Env: `ME_GP2X_NOSMCFREEZE` disables, `ME_GP2X_SMCLOG`
  logs. (Bucketed by a fixed 4KB granularity, **not** `TARGET_PAGE_BITS` — in Unicorn that
  macro is a per-instance runtime value that reads `uc`, unusable at file scope.)
- **`parallel_cflags.py`** — `curr_cflags()` → `CF_PARALLEL` in `qemu/include/exec/exec-all.h`.
  The native-threads engine runs one `uc`/CPU per host thread over one shared host RAM backing,
  so guest atomics must be real host atomics. With `curr_cflags()==0`, `CF_PARALLEL` was never
  set and TCG emitted **non-atomic** `swp`/ldrex — the basis of every LinuxThreads lock/CAS on
  ARMv5. Setting it makes `op_swp`'s `tcg_gen_atomic_xchg_i32` emit a real host atomic on the
  shared backing. (Correctness fix for the native-threads model; the kuser-cmpxchg path is
  already host-atomic. NB: this was not itself the Payback loading-deadlock fix — that was resolved
  separately in the native-threads model.)
- **`fpa_resume.py`** — resume emulation in place after a *handled* invalid instruction
  (`qemu/accel/tcg/cpu-exec.c`). The engine emulates legacy ARM FPA float ops in a
  UC_HOOK_INSN_INVALID callback; stock Unicorn halts the CPU after every handled invalid insn,
  so each FP op cost a full `uc_emu_start` restart (Odonata's per-bullet sin/cos → a few fps).
  Mirrors the UC_HOOK_INTR resume path; unhandled invalid insns still stop with
  `UC_ERR_INSN_INVALID`.
- **`kuser_cmpxchg.py`** — emit the ARM kuser_cmpxchg helper (0xffff0fc0) as an in-TB host-atomic
  `tcg_gen_atomic_cmpxchg_i32` in `qemu/target/arm/translate.c`, instead of the engine's
  `svc #0x90fff0` trap form. EVERY glibc/libstdc++ atomic (each malloc-arena lock, every
  `std::string` refcount) goes through this helper; a Caanoo title that loads many packed keysounds
  (Rhythmos) fires it millions of times, and the svc trap ends a TB + exits the cpu loop each time —
  song-load crawled (~0.06 MIPS, looked like a hang). The in-TB CAS is atomic across the
  native-threads engine's many ucs because `parallel_cflags.py` forces `CF_PARALLEL`. (LDREX/STREX
  can't be used: the exclusive monitor is per-uc and unreliable across our host threads → STREX
  spuriously fails → infinite retry. A direct atomic_cmpxchg has no monitor.) ~1.8x on atomic-heavy
  loading; verified no regression on the threaded GP2X reference (Payback).
- **`mmap_lock.py`** — restore qemu's `mmap_lock()`/`mmap_unlock()` (no-op'd by Unicorn 2.0.1
  for single-uc use) as a real process-global recursive mutex
  (`qemu/include/exec/exec-all.h` + `qemu/accel/tcg/translate-all.c`). Serialises TB
  codegen/invalidation across the native-threads engine's many ucs over one shared RAM backing;
  TB *execution* stays parallel.
- **`mingw_vfree.py`** — fix upstream Unicorn's broken MinGW `qemu_vfree`
  (`qemu/util/oslib-posix.c`): `qemu_try_memalign`'s `__MINGW32__` branch allocates with
  `__mingw_aligned_malloc` (CRT heap) but `qemu_vfree` released it with
  `VirtualFree(ptr, 0, MEM_RELEASE)`. The kernel rounds the address down to its region base, so
  the call usually fails silently (leak) — but when the rounded base coincides with a real
  allocation base it **releases an entire heap region**, corrupting live allocations. This was
  the Windows-only multi-reload teardown crash (`qht_destroy` frees the qht bucket arrays via
  `qemu_vfree` on every `uc_close`); root-caused with PageHeap + TTD, see
  `gp2x-static-titles-and-reload-crash`. Fix: pair with `__mingw_aligned_free`.

## Licensing
Unicorn/qemu are GPLv2; binaries linking the fork make magiceyes GPLv2-compatible.

## Pending fork patches (later Phase-0 / Phase-1)
- Thread-safe `uc_emu_stop` (atomic `cpu->exit_request`) so the timer-slice keeps TCG block
  chaining on — only if the SMC-freeze alone doesn't clear the 30fps gate.
- Larger TCG code-gen buffer (tb_flush stutter) — locate sizing in this qemu version first.
