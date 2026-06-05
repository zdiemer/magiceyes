# Unicorn fork patches (cross-platform engine)

The cross-platform magiceyes engine runs on a **fork of Unicorn 2.0.1** (= qemu's TCG
JIT as a portable library, builds native on Windows/macOS/Linux). The fork carries the
hard-won GP2X fixes we originally made in the qemu-user backend (`host/qemu/apply_gp2x.py`),
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

## Licensing
Unicorn/qemu are GPLv2; binaries linking the fork make magiceyes GPLv2-compatible.

## Pending fork patches (later Phase-0 / Phase-1)
- Thread-safe `uc_emu_stop` (atomic `cpu->exit_request`) so the timer-slice keeps TCG block
  chaining on — only if the SMC-freeze alone doesn't clear the 30fps gate.
- Larger TCG code-gen buffer (tb_flush stutter) — locate sizing in this qemu version first.
