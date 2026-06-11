/* magiceyes Unicorn engine — guest mmap/brk allocator + lazy fault map. */

#include "engine.h"

struct freereg g_mfree[256]; int g_nmfree = 0;
unsigned long g_uc_newmap = 0, g_uc_unmap = 0;   /* diag: JIT-flush triggers (new uc map / uc_mem_unmap) */

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
/* Per-thread "I hold g_reg_lock" flag so the crash guard can release it if a host fault hits a
   guest thread mid-region-op (read_guest's memcpy, ensure_mapped's uc_mem_map_ptr, pram_map)
   -- else every later memory op deadlocks. EVERY g_reg_lock lock/unlock goes through these. */
static __thread int g_holds_reglock = 0;
#define REGLOCK_LOCK()   do { pthread_mutex_lock(&g_reg_lock);   g_holds_reglock = 1; } while (0)
#define REGLOCK_UNLOCK() do { g_holds_reglock = 0; pthread_mutex_unlock(&g_reg_lock); } while (0)
void guard_release_reglock(void) {
    if (g_holds_reglock) { g_holds_reglock = 0; pthread_mutex_unlock(&g_reg_lock); }
}
static int range_free(uint32_t addr, uint32_t len);   /* defined below; used by pram_map */

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
    REGLOCK_LOCK();
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
            g_uc_newmap++;   /* diag: a new uc mapping flushes Unicorn's JIT translation cache */
            if (g_nreg < (int)(sizeof g_reg / sizeof g_reg[0]))
                g_reg[g_nreg++] = (struct gregion){p, len, perms, host, 0};
            p = run;
        }
    }
    REGLOCK_UNLOCK();
}

void map_region(uint32_t addr, uint32_t size, uint32_t perms) {
    ensure_mapped(g_uc, addr, size, perms);
}

int mem_nreg(void) { return g_nreg; }   /* diagnostics: region-registry occupancy (cap = array size) */

/* Host pointer backing guest address gaddr (for host-atomic ops, e.g. kuser cmpxchg). */
void *guest_to_host(uint32_t gaddr) {
    REGLOCK_LOCK();
    struct gregion *r = find_region(ALIGN_DN(gaddr));
    void *h = r ? (uint8_t *)r->host + (gaddr - r->addr) : NULL;
    REGLOCK_UNLOCK();
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
    REGLOCK_LOCK();
    while (len) {
        struct gregion *r = find_region(gaddr);
        if (!r) { REGLOCK_UNLOCK(); return -1; }
        uint32_t avail = r->addr + r->len - gaddr;
        uint32_t n = len < avail ? len : avail;
        memcpy(d, (uint8_t *)r->host + (gaddr - r->addr), n);
        d += n; gaddr += n; len -= n;
    }
    REGLOCK_UNLOCK();
    return 0;
}

/* Free every guest-RAM host backing (the registry owns it; uc_close only drops the uc's
   view of these uc_mem_map_ptr mappings). Called between games by engine_reset_and_load
   AFTER all worker ucs and the main uc are closed -- nothing maps these pointers anymore. */
void mem_reset(void) {
    REGLOCK_LOCK();
    for (int i = 0; i < g_nreg; i++) if (!g_reg[i].external) munmap(g_reg[i].host, g_reg[i].len);
    g_nreg = 0;
    REGLOCK_UNLOCK();
    g_nmfree = 0;
    if (g_pram) { munmap(g_pram, PRAM_SIZE); g_pram = NULL; }   /* shared phys RAM (external regs already dropped) */
}

/* Map [guest,guest+len) onto an EXTERNAL host buffer (not engine-owned) into the current uc and
   record it so every worker uc (uc_map_all) sees the same memory; mem_reset won't free it. Used
   to alias the shim's gp2x_fb mmap onto the engine's g_shm (zero-copy shared framebuffer). */
void mem_register_external(uint32_t guest, uint32_t len, void *host) {
    uint32_t l = ALIGN_UP(len);
    REGLOCK_LOCK();
    if (!find_region(guest)) {
        uc_err e = uc_mem_map_ptr(g_uc, guest, l, UC_PROT_READ | UC_PROT_WRITE, host);
        if (e && e != UC_ERR_MAP) die("uc_mem_map_ptr external", e);
        if (g_nreg < (int)(sizeof g_reg / sizeof g_reg[0]))
            g_reg[g_nreg++] = (struct gregion){guest, l, UC_PROT_READ | UC_PROT_WRITE, host, 1};
    }
    REGLOCK_UNLOCK();
}

/* Map every recorded region into a fresh uc — the native-thread (and ARM940 second-core) factory. */
/* Populate the ARMv5 kuser helper page's trampoline code into u's 0xffff0000 page (NOT the TLS
   slot at 0xffff0ff0, which is per-thread). Shared by the main-uc setup and the per-thread
   private kuser pages in uc_map_all. */
void kuser_populate(uc_engine *u) {
    uint32_t mb = 0xe1a0f00eu;                        /* mov pc,lr (memory_barrier) */
    uc_mem_write(u, 0xffff0fa0u, &mb, 4);
    uint32_t cx[] = {0xef90fff0u, 0xe1a0f00eu};       /* svc #0x90fff0 ; mov pc,lr (cmpxchg) */
    uc_mem_write(u, 0xffff0fc0u, cx, sizeof cx);
    uint32_t gt[] = {0xe59f0008u, 0xe1a0f00eu};       /* get_tls: ldr r0,[pc,#8]; mov pc,lr */
    uc_mem_write(u, 0xffff0fe0u, gt, sizeof gt);
    uint32_t ver = 2; uc_mem_write(u, 0xffff0ffcu, &ver, 4);
    uint32_t tramp[] = {0xe3a070adu, 0xef000000u};    /* SIG_TRAMP: mov r7,#173; svc 0 */
    uc_mem_write(u, 0xffff0f00u, tramp, sizeof tramp);
}

void uc_map_all(uc_engine *u) {
    REGLOCK_LOCK();
    for (int i = 0; i < g_nreg; i++) {
        if (g_reg[i].addr == 0xffff0000u) {
            /* The kuser TLS slot (0xffff0ff0) is PER-THREAD: ARMv5 has no HW TLS register, so the
               real kernel context-switches it. A SHARED backing lets the last thread to start
               clobber every other thread's tls -> the guest's pthread_self()/__kuser_get_tls
               returns the wrong TCB -> two threads see the same ThreadId (Rhythmos's GPAC movie
               player then aborts on its `ProcessLocked != ThreadId()` lock assert). Give each
               worker uc a PRIVATE copy of the page; the trampoline code is identical, only the
               tls differs (written by thread_entry / set_tls). */
            uc_mem_map(u, 0xffff0000u, PAGE, UC_PROT_READ | UC_PROT_EXEC);
            kuser_populate(u);
            continue;
        }
        uc_mem_map_ptr(u, g_reg[i].addr, g_reg[i].len, g_reg[i].perms, g_reg[i].host);
    }
    REGLOCK_UNLOCK();
}

/* ---- shared GP2X physical RAM (upper memory) -------------------------------
 * The GP2X has 64MB SDRAM; games mmap /dev/mem at the upper region (phys 0x02000000+) for
 * framebuffers, the ARM940 code area, and gpu940's shared command buffer. Real hardware has ONE
 * physical RAM, so multiple mmaps of the same phys — by the 920, and by the 940 once it runs —
 * must alias the SAME bytes. Back the whole upper region with one host allocation; every /dev/mem
 * mmap of it becomes a window. (Previously each mmap got its own anon pages, so a second mapping
 * of the same phys saw different memory — the DangerMouse double-map / 940-share bug.) */
uint8_t *g_pram = NULL;
static void ensure_pram(void) {
    if (g_pram) return;
    g_pram = mmap(NULL, PRAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_pram == MAP_FAILED) die("pram mmap", UC_ERR_OK);
}
int phys_in_pram(uint32_t phys, uint32_t len) {
    return phys >= PRAM_BASE && (uint64_t)phys + (len ? len : 1) <= (uint64_t)PRAM_BASE + PRAM_SIZE;
}
/* Allocate the shared physical-RAM backing without mapping it into any uc (the 940 self-test
   loads firmware into it before the 940 uc exists). */
void *pram_ensure(void) { ensure_pram(); return g_pram; }
/* Host pointer for a GP2X physical address in the shared upper RAM (used by the 940 loader). */
void *pram_host(uint32_t phys) {
    if (!g_pram || phys < PRAM_BASE || phys >= PRAM_BASE + PRAM_SIZE) return NULL;
    return g_pram + (phys - PRAM_BASE);
}
/* Map a window of shared physical RAM at a free guest vaddr; returns the guest addr. RWX so the
 * 920 (or a worker uc) can also execute from it if a game runs code out of upper RAM. */
long pram_map(uint32_t phys, uint32_t len, uint32_t hint) {
    ensure_pram();
    uint32_t l = ALIGN_UP(len ? len : 1), at;
    if (hint && range_free(ALIGN_DN(hint), l)) at = ALIGN_DN(hint);
    else {
        uint32_t align = PAGE;
        if (l > PAGE && (l & (l - 1)) == 0) align = l > 0x200000u ? 0x200000u : l;
        g_mmap_next = (g_mmap_next + align - 1) & ~(align - 1);
        at = g_mmap_next; g_mmap_next += l;
    }
    void *host = g_pram + (phys - PRAM_BASE);
    int perms = UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC;
    REGLOCK_LOCK();
    uc_err e = uc_mem_map_ptr(g_uc, at, l, perms, host);
    if (e && e != UC_ERR_MAP) die("pram map", e);
    if (g_nreg < (int)(sizeof g_reg / sizeof g_reg[0]))
        g_reg[g_nreg++] = (struct gregion){at, l, perms, host, 1};   /* external: shared, never freed here */
    REGLOCK_UNLOCK();
    return at;
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
    REGLOCK_LOCK();
    for (int i = 0; i < g_nreg; i++) {
        uint32_t a = g_reg[i].addr, e = a + g_reg[i].len;
        if (addr < e && a < end) { ok = 0; break; }
    }
    REGLOCK_UNLOCK();
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
                if (!getenv("ME_NOZERO")) {        /* recycled memory must read as zero (anon mmap) */
                    /* memset the host backing directly -- one pass, no calloc+memcpy. The old
                       calloc(l)+uc_mem_write(l) was ~3 passes over l plus a host malloc/free per
                       call; a song reloading many MB-sized keysound buffers made that the dominant
                       cost of song-load. A freed region is always one contiguous host-backed
                       gregion (a single mmap), so a flat memset covers it. */
                    void *hp = guest_to_host(at);
                    if (hp) memset(hp, 0, l);
                    else { uint8_t *z = calloc(1, l); if (z) { uc_mem_write(g_uc, at, z, l); free(z); } }
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
    uint32_t at;
    /* /dev/mem mapping of GP2X upper RAM -> a window into the shared physical-RAM backing, so the
       920, repeat mappings, and the ARM940 all alias the same bytes. MMSP2 reg windows
       (phys 0xC0000000 / 0xE0020000) are NOT RAM and keep the per-mapping anon path below. */
    if (type == DEV_MEM && phys_in_pram(phys, len))
        at = (uint32_t)pram_map(phys, len, addr);
    else
        at = do_mmap(addr, len, flags | GMAP_ANON, -1, 0);
    fprintf(stderr, "  DEV mmap type=%d phys=%08x -> guest=%08x len=%08x%s\n",
            type, phys, at, len, (type == DEV_MEM && phys_in_pram(phys, len)) ? " [pram]" : "");
    if (type == DEV_FB) {                                 /* track up to 2 fb buffers */
        /* Advertise/record a synthetic phys per fb (matches FBIOGET_FSCREENINFO.smem_start)
           so an MLC OADR flip — or a blitter dst — that targets that phys resolves back here. */
        if (!g_fb_guest)       { g_fb_guest  = at; record_memmap(0x04000000u, at, len); }
        else if (!g_fb_guest2 && at != g_fb_guest) { g_fb_guest2 = at; record_memmap(0x04040000u, at, len); }
        /* Caanoo (Pollux) firmware menu draws 24bpp BGR into /dev/fb0. We advertise the fbdev as
           24bpp (line_length 960) so its libSDL renders a full 320px-wide surface; present it via the
           Caanoo 24bpp path (reads B,G,R at pitch 960 = 320 px/row, no scaling). */
        if (g_device == 2) { g_caanoo_bpp = 3; g_caanoo_pitch = 960; }
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
