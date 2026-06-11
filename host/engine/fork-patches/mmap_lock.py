#!/usr/bin/env python3
"""Restore qemu's mmap_lock as a real process-global lock in the Unicorn fork.

Authoring/changelog tool (the fork branch is the source of truth; see README.md). Idempotent.

Unicorn 2.0.1 stubbed `mmap_lock()`/`mmap_unlock()` to no-ops (single-uc-per-thread assumption).
The magiceyes native-threads engine runs each guest thread on its own host thread with its own
`uc`/CPUState, all sharing ONE guest-RAM backing (uc_mem_map_ptr). qemu brackets its
code-generation and TB-invalidation paths with mmap_lock()/mmap_unlock() (see
accel/tcg/cpu-exec.c and accel/tcg/translate-all.c) precisely to serialise those mutations
across threads. With the lock no-op'd, two ucs translating / invalidating TBs over the shared
RAM race the qemu page-table (l1_map), code-gen buffer and TB structures -> heap corruption that
surfaces as a DOUBLE-FREE of ARM cp_regs during uc_close->arm_release at reload teardown
(the Windows-only multi-reload crash; root-caused with PageHeap + TTD, see
gp2x-static-titles-and-reload-crash).

Fix: make mmap_lock a real recursive (per-thread-counted) global mutex, exactly as qemu's
linux-user mmap_lock. The lock is held only during codegen/invalidation, NOT during TB
execution, so the native-threads parallelism (the perf win) is preserved.

Usage: python3 mmap_lock.py /path/to/me-unicorn-fork
"""
import sys, os

HDR_OLD = """static inline void mmap_lock(void) {}
static inline void mmap_unlock(void) {}"""
HDR_NEW = """/* magiceyes: real process-global recursive mmap_lock (impl in translate-all.c) --
   restores qemu's codegen/TB-invalidation serialisation for the native-threads engine. */
void mmap_lock(void);
void mmap_unlock(void);
bool have_mmap_lock(void);"""

IMPL_MARK = "magiceyes: real mmap_lock"
IMPL = """
/* ---- magiceyes: real mmap_lock (was a Unicorn no-op) ----------------------
 * Multiple ucs translate/invalidate TBs over one shared guest-RAM backing on
 * parallel host threads; qemu already brackets those paths with mmap_lock(),
 * so making it a real process-global recursive mutex serialises them and ends
 * the heap-corruption/double-free at reload teardown. Held only during
 * codegen/invalidation -> TB execution stays parallel. */
#include <pthread.h>
static pthread_mutex_t me_mmap_mutex = PTHREAD_MUTEX_INITIALIZER;
static __thread int me_mmap_depth;
void mmap_lock(void)   { if (me_mmap_depth++ == 0) pthread_mutex_lock(&me_mmap_mutex); }
void mmap_unlock(void) { if (me_mmap_depth > 0 && --me_mmap_depth == 0) pthread_mutex_unlock(&me_mmap_mutex); }
bool have_mmap_lock(void) { return me_mmap_depth > 0; }
/* -------------------------------------------------------------------------- */
"""

def main(fork):
    # 1) header: turn the no-op inlines into real declarations
    hp = f"{fork}/qemu/include/exec/exec-all.h"
    s = open(hp).read()
    if "void mmap_lock(void);" in s:
        print("mmap_lock: header already patched")
    else:
        if HDR_OLD not in s:
            sys.exit("mmap_lock: no-op mmap_lock/mmap_unlock inlines not found -- fork changed?")
        open(hp, "w").write(s.replace(HDR_OLD, HDR_NEW, 1))
        print("mmap_lock: patched exec-all.h (declarations)")

    # 2) implementation: append to translate-all.c after its includes
    cp = f"{fork}/qemu/accel/tcg/translate-all.c"
    c = open(cp).read()
    if IMPL_MARK in c:
        print("mmap_lock: impl already present"); return
    anchor = '#include "uc_priv.h"'
    if anchor not in c:
        sys.exit("mmap_lock: anchor include not found in translate-all.c -- fork changed?")
    c = c.replace(anchor, anchor + "\n" + IMPL, 1)
    open(cp, "w").write(c)
    print("mmap_lock: patched translate-all.c (implementation)")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else f"{os.path.expanduser('~')}/me-unicorn-fork")
