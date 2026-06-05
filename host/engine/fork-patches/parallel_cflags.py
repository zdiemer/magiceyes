#!/usr/bin/env python3
"""Make the Unicorn fork emit MTTCG-safe (real host-atomic) code for guest atomics.

Authoring/changelog tool (the fork branch is the source of truth; see README.md). Idempotent.

The magiceyes native-threads engine runs each guest thread on its own host thread with its
own `uc`/CPUState, all sharing the SAME guest RAM via uc_mem_map_ptr (one host backing). To
TCG, each uc looks like a single CPU, so `curr_cflags()` (which seeds every TB's cflags)
returns 0 -> CF_PARALLEL is never set -> `tcg_gen_atomic_*` (and the ARM `swp`/ldrex/strex
translation in op_swp/op_ldrex) emit the NON-atomic fallback (plain load+store). Across our
real host threads sharing memory, that makes the guest's atomic primitives non-atomic.

GP2X is ARMv5: LinuxThreads builds EVERY lock/CAS on the `swp` instruction
(__pthread_acquire -> __pthread_compare_and_swap -> __pthread_lock). With non-atomic swp,
two threads can both "win" the same spinlock -> corrupted pthread locks. Setting CF_PARALLEL
makes op_swp's `tcg_gen_atomic_xchg_i32` emit a real host atomic on the shared backing
(correct, since all ucs map the same host pointer). curr_cflags() is the single source used
for both TB codegen and lookup, so flipping it is self-consistent (CF_PARALLEL is in
CF_HASH_MASK). The kuser-cmpxchg path is already host-atomic (syscalls.c 0xfff0); this fixes
the swp path.

Usage: python3 parallel_cflags.py /path/to/me-unicorn-fork
"""
import sys, re

def main(fork):
    p = f"{fork}/qemu/include/exec/exec-all.h"
    s = open(p).read()
    if "magiceyes: native-threads share memory" in s:
        print("parallel_cflags: already applied"); return
    pat = re.compile(r'(static inline uint32_t curr_cflags\(void\)\s*\{\s*)return 0;')
    s2, n = pat.subn(
        r'\1return CF_PARALLEL; /* magiceyes: native-threads share memory -> real host '
        r'atomics for guest swp/ldrex (LinuxThreads locks) */', s, count=1)
    if n != 1:
        sys.exit("parallel_cflags: curr_cflags() 'return 0;' not found -- fork changed?")
    open(p, "w").write(s2)
    print("parallel_cflags: patched curr_cflags() -> CF_PARALLEL")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else f"{__import__('os').path.expanduser('~')}/me-unicorn-fork")
