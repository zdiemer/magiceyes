#!/usr/bin/env python3
"""Fix qemu_vfree's MinGW branch: free the CRT heap, don't VirtualFree it.

Authoring/changelog tool (the fork branch is the source of truth; see README.md). Idempotent.

THE ROOT CAUSE of the Windows-only multi-reload heap-corruption crash
(gp2x-static-titles-and-reload-crash; root-caused 2026-06-10 with PageHeap + TTD).

Upstream Unicorn 2.0.1's qemu/util/oslib-posix.c is the only oslib compiled for MinGW
(CMake's non-MSVC branch), and its __MINGW32__ paths are internally MISMATCHED:

    qemu_try_memalign:  __mingw_aligned_malloc(size, alignment)   // CRT HEAP
    qemu_vfree:         VirtualFree(ptr, 0, MEM_RELEASE)          // VIRTUAL MEMORY

Every qemu_memalign/qemu_vfree pair (qht bucket arrays -- so every qht_destroy in
uc_close -> release_common -- plus exec.c's bounce buffer) therefore VirtualFrees a heap
pointer. NtFreeVirtualMemory rounds the address down to its page/region base, so:
  - usually the rounded base is not an NT region base -> the call FAILS silently -> leak only;
  - when the heap pointer's page-rounded base happens to BE an allocation region base,
    the call SUCCEEDS and RELEASES THE ENTIRE HEAP REGION out from under the allocator.
Live allocations' pages then get recommitted zero-filled by later heap activity ->
wandering, timing-sensitive corruption that surfaces at later *valid* frees (the cp_regs /
"decommitted tcg_ctx" / verifier-busy-list crash signatures). Linux pairs
posix_memalign/free correctly, which is why the bug never reproduced there (and munmap of
an unaligned address fails with EINVAL anyway).

Fix: pair __mingw_aligned_malloc with __mingw_aligned_free.

Usage: python3 mingw_vfree.py /path/to/me-unicorn-fork
"""
import sys, os

OLD = """void qemu_vfree(void *ptr)
{
#ifdef __MINGW32__
    if (ptr) {
        VirtualFree(ptr, 0, MEM_RELEASE);
    }
#else
    //trace_qemu_vfree(ptr);
    free(ptr);
#endif
}"""

NEW = """void qemu_vfree(void *ptr)
{
#ifdef __MINGW32__
    /* magiceyes: qemu_try_memalign's MinGW branch allocates with __mingw_aligned_malloc
       (CRT heap), so freeing with VirtualFree was a mismatched-allocator bug: the kernel
       rounds the address down to its region base, and when that base happens to be a real
       NT allocation base the call SUCCEEDS and releases a whole heap region -> the
       Windows-only timing-sensitive heap corruption at uc_close/reload teardown. */
    __mingw_aligned_free(ptr);
#else
    //trace_qemu_vfree(ptr);
    free(ptr);
#endif
}"""

def main(fork):
    p = f"{fork}/qemu/util/oslib-posix.c"
    s = open(p).read()
    if "__mingw_aligned_free(ptr);" in s:
        print("mingw_vfree: already patched"); return
    if OLD not in s:
        sys.exit("mingw_vfree: expected qemu_vfree body not found -- fork changed?")
    open(p, "w").write(s.replace(OLD, NEW, 1))
    print("mingw_vfree: patched oslib-posix.c (qemu_vfree -> __mingw_aligned_free)")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else f"{os.path.expanduser('~')}/me-unicorn-fork")
