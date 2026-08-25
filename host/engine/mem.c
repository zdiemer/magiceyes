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

/* Snapshot the region registry for an external inspector (the control channel). g_reg is
   file-static and the array is mutated under g_reg_lock by every mapping op, so a caller can
   neither walk it nor hold a pointer into it -- it must take a copy. Returns the number written
   (<= cap); pass cap == 0 to just count. */
int mem_regions(struct me_region *out, int cap) {
    REGLOCK_LOCK();
    int n = g_nreg;
    if (out && cap > 0) {
        if (n > cap) n = cap;
        for (int i = 0; i < n; i++) {
            out[i].addr = g_reg[i].addr;
            out[i].len = g_reg[i].len;
            out[i].perms = g_reg[i].perms;
            out[i].external = g_reg[i].external;
        }
    } else {
        n = g_nreg;
    }
    REGLOCK_UNLOCK();
    return n;
}

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

/* Mirror of read_guest: copy len bytes INTO guest memory, walking region boundaries for exactly
   the same reason (host backings are separate mmaps, not contiguous across regions). Returns 0, or
   -1 if any byte of the range is unmapped -- it never allocates, because a debugger writing
   through a stale pointer must fail rather than silently create memory.
   NOTE: this bypasses TCG's dirty/SMC tracking, so a write into executable memory will not
   invalidate already-translated blocks. Callers patching code must invalidate every uc's cache
   (and see fork-patches/smc_freeze.py: hot pages deliberately stop being SMC-protected). */
int write_guest(const void *src, uint32_t gaddr, uint32_t len) {
    const uint8_t *s = src;
    if (gaddr + len < gaddr) return -1;               /* 32-bit wrap */
    REGLOCK_LOCK();
    while (len) {
        struct gregion *r = find_region(gaddr);
        if (!r) { REGLOCK_UNLOCK(); return -1; }
        uint32_t avail = r->addr + r->len - gaddr;
        uint32_t n = len < avail ? len : avail;
        memcpy((uint8_t *)r->host + (gaddr - r->addr), s, n);
        s += n; gaddr += n; len -= n;
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
        /* Linux MAP_FIXED REPLACES the previous mapping: the range must read as zero (anon) or as
           file content (file-backed, pread below). Keeping old bytes broke ld.so's .bss setup: it
           first maps the whole library FILE, then maps anon zero pages over the memsz>filesz tail
           -- with the old bytes kept, .bss started as the file's string/symbol tables (an
           UNSTRIPPED bundled libSDL gave openjazz-wiz garbage SDL_threads at the first SDL call;
           stripped firmware libs survived only because their .bss span fell past EOF). Zero
           page-by-page: a FIXED range can span multiple host-backed gregions. */
        for (uint32_t o = 0; o < l; o += PAGE) {
            void *hp = guest_to_host(at + o);
            uint32_t chunk = (l - o) < PAGE ? (l - o) : PAGE;
            if (hp) memset(hp, 0, chunk);
        }
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

/* mremap(163): glibc realloc resizes large (mmapped) chunks through this — SDL_mixer and
   tremor decoders realloc sample buffers constantly, so ENOSYS here killed ~24 titles.
   Shrink frees the tail; grow extends in place when the space after is free, else (with
   MREMAP_MAYMOVE) allocates fresh, copies, and frees the old range. */
long do_mremap(uint32_t olda, uint32_t oldl, uint32_t newl, uint32_t flags) {
    if (!olda || (olda & (PAGE - 1)) || !newl) return -22 /*EINVAL*/;
    uint32_t ol = ALIGN_UP(oldl), nl = ALIGN_UP(newl);
    if (nl == ol) return (long)olda;
    if (nl < ol) {                       /* shrink in place: recycle the tail */
        if (g_nmfree < 256) g_mfree[g_nmfree++] = (struct freereg){olda + nl, ol - nl};
        else uc_mem_unmap(g_uc, olda + nl, ol - nl);
        return (long)olda;
    }
    if (range_free(olda + ol, nl - ol)) { /* grow in place */
        map_region(olda + ol, nl - ol, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
        return (long)olda;
    }
    if (!(flags & 1 /*MREMAP_MAYMOVE*/)) return -12 /*ENOMEM*/;
    long na = do_mmap(0, nl, GMAP_ANON, -1, 0);
    if (na <= 0) return -12 /*ENOMEM*/;
    uint8_t *tmp = malloc(ol);
    if (tmp && uc_mem_read(g_uc, olda, tmp, ol) == UC_ERR_OK)
        uc_mem_write(g_uc, (uint32_t)na, tmp, ol);
    free(tmp);
    if (g_nmfree < 256) g_mfree[g_nmfree++] = (struct freereg){olda, ol};
    else uc_mem_unmap(g_uc, olda, ol);
    return na;
}

/* device mmap: give RAM, track phys->guest, and hook MMSP2 reg writes for flips. */
long dev_mmap(int type, uint32_t addr, uint32_t len, uint32_t flags, uint32_t phys, int fbno) {
    uint32_t at;
    /* fb0 vs fb1 for an fbdev mmap comes from the fd's device number (dev_fbno), NOT from
       "second mmap = fb1": SimOniZ's GLBasic runtime mmaps /dev/fb0 a SECOND time beside the
       firmware SDL's mapping and draws through its own view — the old heuristic called that
       fb1 and gave it fresh anon backing, so the game painted a buffer present never read
       (full-speed black). */
    int fb1 = (type == DEV_FB) && fbno == 1;
    uint32_t fbphys = (fb1 ? GP2X_FB1_PHYS : GP2X_FB0_PHYS) + phys;   /* phys = mmap offset here */
    /* fb1's phys is NOT page-aligned (0x25800 apart, real kernel layout). Real fbdev mmap maps
       the containing page and the client adds smem_start's sub-page offset itself; mirror that:
       map the aligned page, return the aligned guest addr, track the pixel base at +delta. */
    uint32_t fbpa = ALIGN_DN(fbphys), fbdelta = fbphys - fbpa;
    /* /dev/mem mapping of GP2X upper RAM -> a window into the shared physical-RAM backing, so the
       920, repeat mappings, and the ARM940 all alias the same bytes. MMSP2 reg windows
       (phys 0xC0000000 / 0xE0020000) are NOT RAM and keep the per-mapping anon path below. */
    uint32_t alias0, alias1;
    int fb_aliased = 0;
    /* Synthetic phys this fbdev range is (or will be) recorded under on non-GP2X devices. */
    uint32_t fbrec = (fb1 ? 0x04040000u : 0x04000000u) + phys;
    if (type == DEV_FB && g_device == 0 && phys_in_pram(fbpa, len + fbdelta)) {
        /* On the real GP2X, fbdev memory IS upper physical RAM (fb0 at 0x03101000, the kernel's
           boot-time MLC scanout; fb1 the page after). minlib-style titles mmap /dev/mem at that
           phys and draw, never writing OADR/EADR — the hardware already scans there. Back the
           fbdev mmap with the shared pram at its real phys so the fbdev view and any /dev/mem
           view alias the same bytes, and present shows what either drew. */
        at = (uint32_t)pram_map(fbpa, len + fbdelta, addr);
    } else if (type == DEV_FB && g_device != 0 &&
               phys_to_guest(fbrec, &alias0) && phys_to_guest(fbrec + len - 1, &alias1) &&
               alias1 - alias0 == len - 1) {
        /* A repeat fbdev mmap of the same fb device (same aliasing contract as the DEV_MEM
           branch below): one framebuffer = one set of bytes. SimOniZ (Wiz GLBasic) draws
           through its own /dev/fb0 view while the firmware SDL's earlier view is what
           present reads — separate backings meant black. */
        at = alias0; fb_aliased = 1;
    } else if (type == DEV_MEM && phys != 0xE0020000u &&
        phys_to_guest(phys, &alias0) && phys_to_guest(phys + len - 1, &alias1) &&
        alias1 - alias0 == len - 1) {
        /* A previously-mapped /dev/mem phys: alias the SAME guest pages (one physical RAM = one set
           of bytes). Lets a second mapper -- e.g. our SDL shim re-mmapping the Pollux video memory
           to read the game's decoded YV12 planes -- see exactly what the first mapper wrote, instead
           of a fresh zero anon region. This now includes repeat maps of the 0xC0000000 reg block
           (malvado's Fenix runtime maps it in both its SDL and its core: separate backings meant a
           register stored via one window read as zero via the other); aliasing returns the FIRST
           window's guest range, whose read/write hooks are already in place. The 0xE0020000
           blitter window keeps the per-mapping path. */
        at = alias0;
    } else if (type == DEV_MEM && phys_in_pram(phys, len))
        at = (uint32_t)pram_map(phys, len, addr);
    else
        at = do_mmap(addr, len, flags | GMAP_ANON, -1, 0);
    fprintf(stderr, "  DEV mmap type=%d phys=%08x -> guest=%08x len=%08x%s\n",
            type, phys, at, len, (type == DEV_MEM && phys_in_pram(phys, len)) ? " [pram]" : "");
    if (type == DEV_FB && !fb_aliased) {                  /* track up to 2 fb buffers */
        /* Record each fb's phys (matches FBIOGET_FSCREENINFO.smem_start) so an MLC OADR flip —
           or a blitter dst — that targets that phys resolves back here. GP2X records the real
           phys (the pram backing above) with the pixel base at +delta past the aligned mapping;
           other devices a synthetic aligned one (offset carried, so a partial repeat map of a
           later page still aliases). */
        uint32_t base = at, rec = fbrec;
        if (g_device == 0) { base = at + fbdelta; rec = fbphys; }
        if (!fb1) {
            if (g_fb_from_devmem) { g_fb_guest = base; g_fb_from_devmem = 0; } /* fbdev view takes over */
            else if (!g_fb_guest) g_fb_guest = base;
            record_memmap(rec, base, len);
        }
        else if (!g_fb_guest2 && base != g_fb_guest) { g_fb_guest2 = base; record_memmap(rec, base, len); }
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
        /* Default MLC scanout: a GP2X title that maps upper RAM over GP2X_FB0_PHYS and never
           opens fbdev nor writes OADR/EADR is drawing straight to the boot-time scanout address
           (minlib single-buffer). Present from there until something more specific — an fbdev
           mmap (which replaces this, aliasing the same pram bytes) or an OADR/EADR write —
           says otherwise. */
        if (g_device == 0 && !g_fb_guest && GP2X_FB0_PHYS >= phys &&
            (uint64_t)GP2X_FB0_PHYS + 320u * 240u * 2u <= (uint64_t)phys + len) {
            g_fb_guest = at + (GP2X_FB0_PHYS - phys);
            g_fb_from_devmem = 1;
        }
        if (phys == 0xC0000000u) {
            g_mmsp2_guest = at;
            static uc_hook hh, hr;
            uc_hook_add(g_uc, &hh, UC_HOOK_MEM_WRITE, mmsp2_write_cb, NULL,
                        at, at + len - 1);
            uc_hook_add(g_uc, &hr, UC_HOOK_MEM_READ, mmsp2_read_cb, NULL,
                        at, at + len - 1);
            /* Pollux reset state: hardware-takeover SDLs (the open2x-wiz "gp2xwiz" driver) READ
               the current video state from the register window instead of fbdev. All-zero
               registers made GP2XWIZ_VideoInit decide NO display engine is enabled (it tests
               bit15 of DPC0 @0x308c / DPC1 @0x348c) and return a wrong-size surface ->
               SDL_video.c "Video mode smaller than requested". Pre-set the primary-LCD DPC
               as ENABLED and both MLCSCREENSIZEs to the 320x240 panel. */
            /* Presets are per-SILICON: the same offsets are different registers on MMSP2 vs
               Pollux, so a GP2X-badged dynamic title must NOT get the Pollux values sprayed
               into its MMSP2 window (or vice versa). */
            if (g_device != 0) {
                uint32_t scr = ((240u - 1) << 16) | (320u - 1);
                uint16_t dpc_on = 0x8000;
                if (len >= 0x4008) uc_mem_write(g_uc, at + 0x4004, &scr, 4);
                if (len >= 0x4408) uc_mem_write(g_uc, at + 0x4404, &scr, 4);
                if (len >= 0x3090) uc_mem_write(g_uc, at + 0x308c, &dpc_on, 2);
            } else {
                /* MMSP2 (GP2X) reset state: paeryn SDL reads the panel geometry from DPC_X_MAX
                   @0x2816 / DPC_Y_MAX @0x2818 (value = size-1, mmsp2_regs.h). Zeros made it derive
                   a 1x1 physical screen, and its hardware-scaler math (1024*w/phys_width) then
                   sheared every scaled surface (UQM). The LCD is 320x240 progressive. */
                if (len >= 0x281a && !getenv("ME_NO_DPC_PRESET")) {
                    uint16_t xmax = 320 - 1, ymax = 240 - 1;
                    uc_mem_write(g_uc, at + 0x2816, &xmax, 2);
                    uc_mem_write(g_uc, at + 0x2818, &ymax, 2);
                }
            }
        }
        if (phys >= 0xE0020000u && phys < 0xE0028000u && !getenv("ME_GP2X_NOBLIT")) {
            /* MMSP2 2D blitter window: trap writes -> run blits; reads -> serve STATUS=idle
               (synchronous). The register file is MIRRORED through the whole 0xE002xxxx
               block: SmashGp2x maps 0xE0024000 and programs the identical MESG layout
               there (dst/src/strides/size/STATUS-go), so accept any page of the block --
               the hook works on window-relative offsets either way. */
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
