/* magiceyes Unicorn engine — guest mmap/brk allocator + lazy fault map. */

#include "engine.h"

struct freereg g_mfree[256]; int g_nmfree = 0;

/* unified mmap for old_mmap(90) and mmap2(192); file-backed reads via pread. */
long do_mmap(uint32_t addr, uint32_t len, uint32_t flags, int fd, uint64_t off) {
    uint32_t l = ALIGN_UP(len ? len : 1);
    uint32_t at = 0;
    if ((flags & GMAP_FIXED) && addr) {
        at = ALIGN_DN(addr);
        map_region(at, l, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
    } else {
        for (int i = 0; i < g_nmfree; i++)        /* reuse a freed same-size region */
            if (g_mfree[i].len == l) { at = g_mfree[i].addr; g_mfree[i] = g_mfree[--g_nmfree]; break; }
        if (at) {
            if (!getenv("ME_NOZERO")) {          /* anon mmap memory must read as zero */
                uint8_t *z = calloc(1, l);
                if (z) { uc_mem_write(g_uc, at, z, l); free(z); }
            }
        } else {
            /* align power-of-two allocs to their size (2MB thread stacks must be
               2MB-aligned so sp&~(size-1) finds the LinuxThreads descriptor). */
            uint32_t align = PAGE;
            if (l > PAGE && (l & (l - 1)) == 0) align = l > 0x200000u ? 0x200000u : l;
            g_mmap_next = (g_mmap_next + align - 1) & ~(align - 1);
            at = g_mmap_next; g_mmap_next += l;
            map_region(at, l, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
        }
    }
    if (!(flags & GMAP_ANON) && fd >= 0 && len) {
        uint8_t *tmp = malloc(len);
        ssize_t n = pread(fd, tmp, len, (off_t)off);
        if (n > 0) uc_mem_write(g_uc, at, tmp, n);
        free(tmp);
    }
    return at;
}

/* device mmap: give RAM, track phys->guest, and hook MMSP2 reg writes for flips. */
long dev_mmap(int type, uint32_t addr, uint32_t len, uint32_t flags, uint32_t phys) {
    uint32_t at = do_mmap(addr, len, flags | GMAP_ANON, -1, 0);
    fprintf(stderr, "  DEV mmap type=%d phys=%08x -> guest=%08x len=%08x\n",
            type, phys, at, len);
    if (type == DEV_FB) {                                 /* track up to 2 fb buffers */
        if (!g_fb_guest) g_fb_guest = at;
        else if (!g_fb_guest2 && at != g_fb_guest) g_fb_guest2 = at;
    }
    if (type == DEV_MEM) {
        record_memmap(phys, at, len);
        if (phys == 0xC0000000u) {
            g_mmsp2_guest = at;
            static uc_hook hh, hr;
            uc_hook_add(g_uc, &hh, UC_HOOK_MEM_WRITE, mmsp2_write_cb, NULL,
                        at, at + len - 1);
            uc_hook_add(g_uc, &hr, UC_HOOK_MEM_READ, mmsp2_read_cb, NULL,
                        at, at + len - 1);
        }
    }
    return at;
}

/* fill an OABI `struct stat` (ARM: st_mode@8, st_size@20) — enough for size. */
/* Read a NUL-terminated guest string safely. A fixed-size uc_mem_read can over-read
 * past the end of the mapped region and fail, leaving the buffer garbage -> spurious
 * ENOENT / "can't stat" on files that exist (intermittent, depending on where the game
 * allocated the path). Read bounded, stopping at the NUL or first unreadable byte. */
bool mem_invalid_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                           int size, int64_t value, void *user) {
    (void)value; (void)user;
    g_n_fault++;
    static int n = 0;
    uint32_t a = (uint32_t)addr;
    if (n++ < 60)
        fprintf(stderr, "  mem-fault type=%d @ %08x size=%d pc=%08x lr=%08x sp=%08x tid=%d\n",
                type, a, size, gread(UC_ARM_REG_PC), gread(UC_ARM_REG_LR),
                gread(UC_ARM_REG_SP), g_th[g_cur].tid);
    if (a < 0x10000 && n < 62) {   /* likely a bad pointer: dump regs to find its source */
        static const int rr[13] = {UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
            UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,
            UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12};
        fprintf(stderr, "   regs:");
        for (int i = 0; i < 13; i++) fprintf(stderr, " r%d=%08x", i, gread(rr[i]));
        fprintf(stderr, "\n");
    }
    uc_mem_map(uc, ALIGN_DN(a), PAGE, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
    return true;  /* retry the faulting access */
}

/* Preemption: a host timer thread stops emulation every few ms. Unlike a per-block
   hook or uc_emu_start's instruction-count limit (both disable Unicorn's block
   chaining -> ~21 MIPS), an external uc_emu_stop keeps chaining enabled (fast) while
   still letting the main loop time-slice between guest threads. */
