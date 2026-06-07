/* magiceyes Unicorn engine — guest mmap/brk allocator + lazy fault map. */

#include "engine.h"

struct freereg g_mfree[256]; int g_nmfree = 0;

/* ---- host-backed guest memory ----------------------------------------------
 * Every guest region is backed by a host allocation and mapped via uc_mem_map_ptr,
 * so additional uc instances (one per native guest thread, the native-threads model)
 * can share the SAME memory by mapping the same host pointers. The registry records
 * each region for that factory (uc_map_all). */
/* external=1: host backing is owned elsewhere (the shm framebuffer is the engine's g_shm,
   shared with the viewer) -> map it into every uc but NEVER munmap it on mem_reset. */
struct gregion { uint32_t addr, len; int perms; void *host; int external; };
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
                g_reg[g_nreg++] = (struct gregion){p, len, perms, host, 0};
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

/* Copy len bytes of guest memory at gaddr into dst, walking region boundaries: a guest buffer
   is contiguous in GUEST space but its host backings are separate mmaps (one per ensure_mapped
   range) that are NOT contiguous in host space -- so a guest texture/array spanning two regions
   cannot be read through a single host pointer (works by luck on Linux where the mmaps land
   adjacent; faults on Windows where they don't). This stitches the pieces. Returns 0 on success,
   -1 if any byte of the range is unmapped (caller treats that as "no data"). */
int read_guest(void *dst, uint32_t gaddr, uint32_t len) {
    uint8_t *d = dst;
    if (gaddr + len < gaddr) return -1;               /* 32-bit wrap */
    pthread_mutex_lock(&g_reg_lock);
    while (len) {
        struct gregion *r = find_region(gaddr);
        if (!r) { pthread_mutex_unlock(&g_reg_lock); return -1; }
        uint32_t avail = r->addr + r->len - gaddr;
        uint32_t n = len < avail ? len : avail;
        memcpy(d, (uint8_t *)r->host + (gaddr - r->addr), n);
        d += n; gaddr += n; len -= n;
    }
    pthread_mutex_unlock(&g_reg_lock);
    return 0;
}

/* Free every guest-RAM host backing (the registry owns it; uc_close only drops the uc's
   view of these uc_mem_map_ptr mappings). Called between games by engine_reset_and_load
   AFTER all worker ucs and the main uc are closed -- nothing maps these pointers anymore. */
void mem_reset(void) {
    pthread_mutex_lock(&g_reg_lock);
    for (int i = 0; i < g_nreg; i++) if (!g_reg[i].external) munmap(g_reg[i].host, g_reg[i].len);
    g_nreg = 0;
    pthread_mutex_unlock(&g_reg_lock);
    g_nmfree = 0;
}

/* Map [guest,guest+len) onto an EXTERNAL host buffer (not engine-owned) into the current uc and
   record it so every worker uc (uc_map_all) sees the same memory; mem_reset won't free it. Used
   to alias the shim's gp2x_fb mmap onto the engine's g_shm (zero-copy shared framebuffer). */
void mem_register_external(uint32_t guest, uint32_t len, void *host) {
    uint32_t l = ALIGN_UP(len);
    pthread_mutex_lock(&g_reg_lock);
    if (!find_region(guest)) {
        uc_err e = uc_mem_map_ptr(g_uc, guest, l, UC_PROT_READ | UC_PROT_WRITE, host);
        if (e && e != UC_ERR_MAP) die("uc_mem_map_ptr external", e);
        if (g_nreg < (int)(sizeof g_reg / sizeof g_reg[0]))
            g_reg[g_nreg++] = (struct gregion){guest, l, UC_PROT_READ | UC_PROT_WRITE, host, 1};
    }
    pthread_mutex_unlock(&g_reg_lock);
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

/* True if no recorded region overlaps [addr,addr+len) -- i.e. the range is free to map. */
static int range_free(uint32_t addr, uint32_t len) {
    uint32_t end = addr + len; if (end < addr) return 0;   /* wrap */
    int ok = 1;
    pthread_mutex_lock(&g_reg_lock);
    for (int i = 0; i < g_nreg; i++) {
        uint32_t a = g_reg[i].addr, e = a + g_reg[i].len;
        if (addr < e && a < end) { ok = 0; break; }
    }
    pthread_mutex_unlock(&g_reg_lock);
    return ok;
}

/* unified mmap for old_mmap(90) and mmap2(192); file-backed reads via pread. */
long do_mmap(uint32_t addr, uint32_t len, uint32_t flags, int fd, uint64_t off) {
    uint32_t l = ALIGN_UP(len ? len : 1);
    uint32_t at = 0;
    if ((flags & GMAP_FIXED) && addr) {
        at = ALIGN_DN(addr);
        map_region(at, l, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
    } else {
        /* Honor a non-FIXED address hint when the range is free (Linux mmap semantics). glibc's
           malloc arena allocator and LinuxThreads stack placement probe DESCENDING hint addresses
           and retry (munmap + re-mmap) until they get the address they asked for; ignoring the hint
           made the dynamic-glibc firmware menu loop ~500x at startup -- slow-but-OK on Linux, a
           multi-second "black screen" on Windows where each mmap is far dearer. Honoring it = 1 try. */
        if (addr && range_free(ALIGN_DN(addr), l)) {
            at = ALIGN_DN(addr);                  /* fresh anon mapping -> reads as zero */
            map_region(at, l, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
        } else {
            int reused = 0;
            for (int i = 0; i < g_nmfree; i++)    /* reuse a freed same-size region */
                if (g_mfree[i].len == l) { at = g_mfree[i].addr; g_mfree[i] = g_mfree[--g_nmfree]; reused = 1; break; }
            if (reused) {
                if (!getenv("ME_NOZERO")) {        /* recycled memory must read as zero */
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
        /* Advertise/record a synthetic phys per fb (matches FBIOGET_FSCREENINFO.smem_start)
           so an MLC OADR flip — or a blitter dst — that targets that phys resolves back here. */
        if (!g_fb_guest)       { g_fb_guest  = at; record_memmap(0x04000000u, at, len); }
        else if (!g_fb_guest2 && at != g_fb_guest) { g_fb_guest2 = at; record_memmap(0x04040000u, at, len); }
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
        if (phys == 0xE0020000u && !getenv("ME_GP2X_NOBLIT")) {   /* MMSP2 2D blitter window: trap
                                        writes -> run blits; reads -> serve STATUS=idle (synchronous) */
            g_blit_guest = at;
            static uc_hook bh, br;
            uc_hook_add(g_uc, &bh, UC_HOOK_MEM_WRITE, blitter_write_cb, NULL,
                        at, at + len - 1);
            uc_hook_add(g_uc, &br, UC_HOOK_MEM_READ, blitter_read_cb, NULL,
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
