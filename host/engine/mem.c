/* magiceyes Unicorn engine — guest mmap/brk allocator + lazy fault map. */

#include "engine.h"

struct freereg g_mfree[256]; int g_nmfree = 0;

/* ---- host-backed guest memory ----------------------------------------------
 * Every guest region is backed by a host allocation and mapped via uc_mem_map_ptr,
 * so additional uc instances (one per native guest thread, the native-threads model)
 * can share the SAME memory by mapping the same host pointers. The registry records
 * each region for that factory (uc_map_all). */
struct gregion { uint32_t addr, len; int perms; void *host; };
static struct gregion g_reg[2048];
static int g_nreg = 0;
static pthread_mutex_t g_reg_lock = PTHREAD_MUTEX_INITIALIZER;

static struct gregion *find_region(uint32_t pg) {   /* registry entry covering guest page pg */
    for (int i = 0; i < g_nreg; i++)
        if (pg >= g_reg[i].addr && pg < g_reg[i].addr + g_reg[i].len) return &g_reg[i];
    return NULL;
}

/* Ensure [addr,size) is mapped into uc `u`, SHARING the registry's host backing so every
   uc (one per native guest thread) sees the same memory. A page already backed anywhere
   is mapped into u from that same host allocation (dup map -> UC_ERR_MAP, ignored); a new
   range gets one host allocation + record. This is the correctness crux for native threads:
   a thread faulting on memory another thread created must get the SAME host backing, not a
   fresh one. */
void ensure_mapped(uc_engine *u, uint32_t addr, uint32_t size, int perms) {
    uint32_t end = ALIGN_UP(addr + size), p = ALIGN_DN(addr);
    pthread_mutex_lock(&g_reg_lock);
    while (p < end) {
        struct gregion *r = find_region(p);
        if (r) {                                  /* existing backing -> map it into u */
            uc_mem_map_ptr(u, r->addr, r->len, r->perms, r->host);
            p = r->addr + r->len;
        } else {                                  /* new range up to the next existing region */
            uint32_t run = p;
            while (run < end && !find_region(run)) run += PAGE;
            uint32_t len = run - p;
            void *host = mmap(NULL, len, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (host == MAP_FAILED) die("host mmap", UC_ERR_OK);
            uc_err e = uc_mem_map_ptr(u, p, len, perms, host);
            if (e && e != UC_ERR_MAP) die("uc_mem_map_ptr", e);
            if (g_nreg < (int)(sizeof g_reg / sizeof g_reg[0]))
                g_reg[g_nreg++] = (struct gregion){p, len, perms, host};
            p = run;
        }
    }
    pthread_mutex_unlock(&g_reg_lock);
}

void map_region(uint32_t addr, uint32_t size, uint32_t perms) {
    ensure_mapped(g_uc, addr, size, perms);
}

/* Host pointer backing guest address gaddr (for host-atomic ops, e.g. kuser cmpxchg). */
void *guest_to_host(uint32_t gaddr) {
    pthread_mutex_lock(&g_reg_lock);
    struct gregion *r = find_region(ALIGN_DN(gaddr));
    void *h = r ? (uint8_t *)r->host + (gaddr - r->addr) : NULL;
    pthread_mutex_unlock(&g_reg_lock);
    return h;
}

/* Free every guest-RAM host backing (the registry owns it; uc_close only drops the uc's
   view of these uc_mem_map_ptr mappings). Called between games by engine_reset_and_load
   AFTER all worker ucs and the main uc are closed -- nothing maps these pointers anymore. */
void mem_reset(void) {
    pthread_mutex_lock(&g_reg_lock);
    for (int i = 0; i < g_nreg; i++) munmap(g_reg[i].host, g_reg[i].len);
    g_nreg = 0;
    pthread_mutex_unlock(&g_reg_lock);
    g_nmfree = 0;
}

/* Map every recorded region into a fresh uc — the native-thread factory. */
void uc_map_all(uc_engine *u) {
    pthread_mutex_lock(&g_reg_lock);
    for (int i = 0; i < g_nreg; i++)
        uc_mem_map_ptr(u, g_reg[i].addr, g_reg[i].len, g_reg[i].perms, g_reg[i].host);
    pthread_mutex_unlock(&g_reg_lock);
}

/* TEMP (ME_FBWATCH): count guest writes into the framebuffer to tell whether the render code
   runs (writes happen) vs a memory-aliasing bug (no writes but present sees content elsewhere). */
unsigned long g_fbwrites = 0;
void fbwatch_cb(uc_engine *uc, uc_mem_type type, uint64_t addr, int size, int64_t value, void *user) {
    (void)uc; (void)type; (void)size; (void)value; (void)user;
    if (++g_fbwrites <= 3 || (g_fbwrites % 200000) == 0)
        fprintf(stderr, "FBWRITE #%lu @ %08x = %08x tid=%d pc=%08x\n", g_fbwrites,
                (uint32_t)addr, (uint32_t)value, g_self ? g_self->tid : -1, gread(UC_ARM_REG_PC));
}

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
        if (getenv("ME_FBWATCH")) {   /* TEMP: count guest writes into this fb (execution vs aliasing) */
            extern void fbwatch_cb(uc_engine*, uc_mem_type, uint64_t, int, int64_t, void*);
            static uc_hook fbh; uc_hook_add(g_uc, &fbh, UC_HOOK_MEM_WRITE, fbwatch_cb, NULL,
                                            at, at + len - 1);
        }
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
                gread(UC_ARM_REG_SP), g_self ? g_self->tid : -1);
    if (a < 0x10000 && n < 62) {   /* likely a bad pointer: dump regs to find its source */
        static const int rr[13] = {UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
            UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,
            UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12};
        fprintf(stderr, "   regs:");
        for (int i = 0; i < 13; i++) fprintf(stderr, " r%d=%08x", i, gread(rr[i]));
        fprintf(stderr, "\n   stack ret-addrs:");
        uint32_t sp = gread(UC_ARM_REG_SP);
        for (int k = 0; k < 96; k++) {
            uint32_t w = 0; uc_mem_read(g_uc, sp + k * 4, &w, 4);
            if (w >= 0x8100 && w < 0x19bc00) fprintf(stderr, " %08x", w);  /* .text range */
        }
        uint32_t obj = gread(UC_ARM_REG_R4), vt = 0, da = 0;
        uc_mem_read(g_uc, obj + 0x94, &vt, 4);
        if (vt) uc_mem_read(g_uc, vt + 0x38, &da, 4);
        fprintf(stderr, "\n   FILE r4=%08x  vtable(+0x94)=%08x  __doallocate(vt+0x38)=%08x\n   r4[0..0x20]:",
                obj, vt, da);
        for (int k = 0; k < 8; k++) { uint32_t w = 0; uc_mem_read(g_uc, obj + k * 4, &w, 4);
                                      fprintf(stderr, " %08x", w); }
        fprintf(stderr, "\n");
    }
    map_region(ALIGN_DN(a), PAGE, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
    return true;  /* retry the faulting access */
}

/* Preemption: a host timer thread stops emulation every few ms. Unlike a per-block
   hook or uc_emu_start's instruction-count limit (both disable Unicorn's block
   chaining -> ~21 MIPS), an external uc_emu_stop keeps chaining enabled (fast) while
   still letting the main loop time-slice between guest threads. */
