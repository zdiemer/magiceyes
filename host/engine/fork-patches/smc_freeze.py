#!/usr/bin/env python3
"""Apply the GP2X SMC-freeze to the Unicorn fork's softmmu TLB path.

This is the authoring/changelog tool: it edits the fork's qemu/accel/tcg/cputlb.c,
which is then committed into the fork's git (the fork branch is the source of truth,
not this script). Idempotent.

GP2X games co-locate hot .iwram scratch DATA with hot CODE on one guest page, so every
data store to such a page triggers a false self-modifying-code TB invalidation
(notdirty_write -> tb_invalidate_phys_page_fast). On qemu-user this capped Payback
gameplay at 6.6fps; the fix there (host/qemu/apply_gp2x.py) lived in user-exec.c's
page_protect. Unicorn is softmmu, so the analog is two spots in cputlb.c:
  1. notdirty_write(): once a page thrashes past a threshold, SKIP the TB invalidation.
  2. tlb_set_page_with_attrs(): stop re-arming TLB_NOTDIRTY for frozen pages, so data
     writes stop trapping entirely.
GP2X installs IWRAM code once at startup, so freezing co-located code TBs is safe.
Env: ME_GP2X_NOSMCFREEZE disables it; ME_GP2X_SMCLOG logs the fault rate / freezes.

Usage: python3 smc_freeze.py /path/to/me-unicorn-fork
"""
import os, sys

HELPER = r'''
/* ---- magiceyes: GP2X SMC-freeze (softmmu) ----------------------------------
 * Hot .iwram scratch DATA co-located with hot CODE on one guest page triggers a
 * false self-modifying-code TB invalidation on every data store (page thrash).
 * Once a page clearly thrashes we stop SMC-protecting it. Indexed by physical RAM
 * page over the low 64MB (all GP2X RAM-resident code/data). Single-instance use
 * (me_unicorn drives one uc engine); the static arrays would need to move onto uc->
 * for a multi-instance build -- noted as future cleanup.
 *
 * NB: bucket by a FIXED 4KB granularity, NOT TARGET_PAGE_BITS -- in Unicorn the
 * TARGET_PAGE_* macros are per-instance runtime values that expand to read `uc`, so
 * they can't be used at file scope or in helpers that don't take a uc. A coarser-than-
 * TLB bucket is fine: the .iwram thrash region is contiguous, so freezing a 4KB span
 * after the threshold is correct. */
#define GP2X_PAGE_BITS     12u
#define GP2X_PAGE_MASK     (~(ram_addr_t)((1u << GP2X_PAGE_BITS) - 1u))
#define GP2X_FREEZE_PAGES  (0x4000000u >> GP2X_PAGE_BITS)   /* low 64MB / 4KB */
#define GP2X_FREEZE_THRESH 512
static uint16_t gp2x_smc_faults[GP2X_FREEZE_PAGES];
static uint8_t  gp2x_smc_frozen[GP2X_FREEZE_PAGES];
static int gp2x_smc_off = -1;   /* ME_GP2X_NOSMCFREEZE */
static int gp2x_smc_log = -1;   /* ME_GP2X_SMCLOG */
static unsigned long gp2x_smc_total;

static inline bool gp2x_page_frozen(ram_addr_t ra)
{
    ram_addr_t pg = ra >> GP2X_PAGE_BITS;
    return pg < GP2X_FREEZE_PAGES && gp2x_smc_frozen[pg];
}

/* Note an SMC (notdirty) fault; returns true if the page is frozen -> caller must
 * SKIP the TB invalidation. */
static bool gp2x_smc_note_fault(ram_addr_t ra)
{
    ram_addr_t pg;
    if (gp2x_smc_off < 0) {
        gp2x_smc_off = getenv("ME_GP2X_NOSMCFREEZE") ? 1 : 0;
        gp2x_smc_log = getenv("ME_GP2X_SMCLOG") ? 1 : 0;
    }
    gp2x_smc_total++;
    if (gp2x_smc_log && (gp2x_smc_total % 20000) == 0) {
        fprintf(stderr, "[gp2x] SMC notdirty faults=%lu last_pg=%08x\n",
                gp2x_smc_total, (unsigned)(ra & GP2X_PAGE_MASK));
    }
    if (gp2x_smc_off) {
        return false;
    }
    pg = ra >> GP2X_PAGE_BITS;
    if (pg >= GP2X_FREEZE_PAGES) {
        return false;
    }
    if (gp2x_smc_frozen[pg]) {
        return true;
    }
    if (gp2x_smc_faults[pg] < 0xffff &&
        ++gp2x_smc_faults[pg] >= GP2X_FREEZE_THRESH) {
        gp2x_smc_frozen[pg] = 1;
        if (gp2x_smc_log) {
            fprintf(stderr, "[gp2x] SMC-freeze page=%08x (stopped %d-fault thrash)\n",
                    (unsigned)(ra & GP2X_PAGE_MASK), GP2X_FREEZE_THRESH);
        }
        return true;
    }
    return false;
}
/* -------------------------------------------------------------------------- */
'''

NOTDIRTY_OLD = """    if (!cpu_physical_memory_get_dirty_flag(ram_addr, DIRTY_MEMORY_CODE)) {
        struct page_collection *pages
            = page_collection_lock(cpu->uc, ram_addr, ram_addr + size);
        tb_invalidate_phys_page_fast(cpu->uc, pages, ram_addr, size, retaddr);
        page_collection_unlock(pages);
    }"""

NOTDIRTY_NEW = """    if (!cpu_physical_memory_get_dirty_flag(ram_addr, DIRTY_MEMORY_CODE)) {
        /* magiceyes: count the fault; skip the invalidation on frozen GP2X pages */
        if (!gp2x_smc_note_fault(ram_addr)) {
            struct page_collection *pages
                = page_collection_lock(cpu->uc, ram_addr, ram_addr + size);
            tb_invalidate_phys_page_fast(cpu->uc, pages, ram_addr, size, retaddr);
            page_collection_unlock(pages);
        }
    }"""

TLBARM_OLD = """            } else if (cpu_physical_memory_is_clean(iotlb)) {
                write_address |= TLB_NOTDIRTY;
            }"""

TLBARM_NEW = """            } else if (cpu_physical_memory_is_clean(iotlb)
                       && !gp2x_page_frozen(iotlb)) {
                /* magiceyes: don't re-arm NOTDIRTY on frozen GP2X pages */
                write_address |= TLB_NOTDIRTY;
            }"""


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: smc_freeze.py <fork-root>")
    path = os.path.join(sys.argv[1], "qemu", "accel", "tcg", "cputlb.c")
    s = open(path).read()
    if "gp2x_smc_note_fault" in s:
        print("  cputlb.c already patched (SMC-freeze)")
        return
    anchor = "#include <glib_compat.h>\n"
    if s.count(anchor) != 1:
        sys.exit("ERROR: glib_compat include anchor not unique/found")
    s = s.replace(anchor, anchor + HELPER, 1)
    if s.count(NOTDIRTY_OLD) != 1:
        sys.exit("ERROR: notdirty_write block not unique/found")
    s = s.replace(NOTDIRTY_OLD, NOTDIRTY_NEW, 1)
    if s.count(TLBARM_OLD) != 1:
        sys.exit("ERROR: TLB notdirty-arm block not unique/found")
    s = s.replace(TLBARM_OLD, TLBARM_NEW, 1)
    open(path, "w").write(s)
    print("  cputlb.c patched (SMC-freeze: notdirty_write skip + TLB arm guard)")


if __name__ == "__main__":
    main()
