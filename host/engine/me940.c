/* magiceyes — ARM940T second-core emulation. See host/engine/ARM940.md for the full design.
 *
 * The GP2X's auxiliary ARM940T shares physical RAM with the main ARM920 and is started by MMSP2
 * registers. We run it as a SECOND Unicorn instance over the shared g_pram backing, on its own
 * host thread. It is bare-metal (no Linux syscalls, no LinuxThreads): it just executes and touches
 * shared memory + a few MMSP2 registers, synchronising with the 920 via the cyclic command buffer
 * (gpu940) like real hardware — so it does NOT take the 920 biglock.
 *
 * Control regs (MMSP2 @0xC0000000): SYSCLKENREG 0x904 bit0 = clock; DUALCTRL940 0x3b48 bit7 =
 * halt(1)/run(0), bits0-6 = memory bank (940 addr 0 -> phys bank*0x1000000). gpu940 uses bank 2,
 * so the 940 sees phys 0x02000000 (firmware) at addr 0 = the base of g_pram. */
#include "engine.h"
#include <pthread.h>

#define DUALCTRL940 0x3b48u
#define SYSCLKEN    0x904u
#define DUALINT920  0x3b40u   /* 920 <- 940 */
#define DUALINT940  0x3b42u   /* 940 <- 920 */
/* The 940 sees the MMSP2 register block at 0xBE000000 (NOT the 920's 0xC0000000): the gpu940
   firmware reads this base from its global @0x6a90 (= 0xbe000000) and indexes the MLC regs off it.
   Matches the firmware's CP15 MPU region base. */
#define MMSP2_PHYS  0xBE000000u
#define MMSP2_LEN   0x10000u

static uc_engine *g_uc940;
static pthread_t  g_th940;
static int        g_940_running = 0;
static int        g_940_clock   = 0;
static int        g_940_bank    = -1;
static volatile int g_940_stop  = 0;
static uint8_t   *g_mmsp940;          /* the 940's own view of the MMSP2 register page */

int me940_active(void) { return g_940_running; }

/* ME_940_TRACE activity probe: PC coverage tells whether the 940 only polls its command queue
   (~main 0x6b8..) or actually runs the rasterizer (other firmware functions = real work). */
static int g_940_trace = -1;
static long g_940_insns = 0; static uint32_t g_940_pcmin = 0xffffffff, g_940_pcmax = 0;
static uint8_t g_940_seen[64];   /* which 1KB code buckets the PC visited (firmware is ~36KB) */
static void me940_trace_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)uc; (void)size; (void)user;
    uint32_t pc = (uint32_t)addr;
    g_940_insns++;
    if (pc < g_940_pcmin) g_940_pcmin = pc;
    if (pc > g_940_pcmax) g_940_pcmax = pc;
    if (pc < sizeof g_940_seen * 1024) g_940_seen[pc >> 10] = 1;
}

/* The 940's MMSP2 access (DUALINT etc.). gpu940 sets up the MLC via 0x2880/0x2916-0x291c, but its
   declared scanout register doesn't point at the live framebuffer (that lives in a video buffer it
   renders into); present follows the rendered buffer by content (me940_scan_fb). So here we just
   store writes (so reads see them) and optionally trace the MLC programming. */
static void me940_mmio(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                       int64_t value, void *user) {
    (void)uc; (void)user;
    if (type != UC_MEM_WRITE) return;
    uint32_t off = (uint32_t)addr - MMSP2_PHYS;
    if (off + (uint32_t)size > MMSP2_LEN) return;
    memcpy(g_mmsp940 + off, &value, size);                 /* store (reads see it) */
    if (g_940_trace > 0 && off >= 0x2800 && off <= 0x2960) {   /* trace the 940's MLC/display writes */
        static int n = 0; if (n++ < 80)
            fprintf(stderr, "[940 mlc] %04x = %08x\n", off, (uint32_t)value);
    }
}

static void *me940_thread(void *arg) {
    (void)arg;
    /* Run the firmware from its reset vector (addr 0). Returns on uc_emu_stop (clean halt) or a
       fault; either way the 940 just stops — it must never tear down the 920/the window. */
    uc_err e = uc_emu_start(g_uc940, 0, PRAM_SIZE, 0, 0);
    if (e != UC_ERR_OK && !g_940_stop) {
        uint32_t pc = 0; uc_reg_read(g_uc940, UC_ARM_REG_PC, &pc);
        fprintf(stderr, "[940] stopped on fault: %s pc=%08x\n", uc_strerror(e), pc);
    }
    return NULL;
}

void me940_start(int bank) {
    if (g_940_running) return;
    if (!g_pram) { fprintf(stderr, "[940] no shared RAM (g_pram); cannot start\n"); return; }
    uint32_t phys0 = (uint32_t)bank * 0x1000000u;
    if (phys0 != PRAM_BASE) {   /* only bank 2 (gpu940) is modelled: 940 addr 0 == g_pram[0] */
        fprintf(stderr, "[940] bank %d -> phys %08x not modelled (only bank 2 / 0x02000000)\n", bank, phys0);
        return;
    }
    uc_err e = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &g_uc940);
    if (e) { fprintf(stderr, "[940] uc_open: %s\n", uc_strerror(e)); return; }
    uc_ctl_set_cpu_model(g_uc940, UC_CPU_ARM_946);   /* MPU-equipped (CP15 cr2/3/5/6) like the 940T */
    /* shared upper RAM at 940 addr 0 (addr A -> phys 0x02000000+A) */
    if ((e = uc_mem_map_ptr(g_uc940, 0, PRAM_SIZE, UC_PROT_ALL, g_pram))) {
        fprintf(stderr, "[940] map pram: %s\n", uc_strerror(e)); uc_close(g_uc940); g_uc940 = NULL; return;
    }
    /* the MMSP2 register block (gpu940 pokes DUALINT here to signal the 920) */
    if (!g_mmsp940) g_mmsp940 = calloc(1, MMSP2_LEN);
    uc_mem_map_ptr(g_uc940, MMSP2_PHYS, MMSP2_LEN, UC_PROT_READ | UC_PROT_WRITE, g_mmsp940);
    uc_hook h;
    uc_hook_add(g_uc940, &h, UC_HOOK_MEM_WRITE, me940_mmio, NULL, MMSP2_PHYS, MMSP2_PHYS + MMSP2_LEN - 1);
    uint32_t z = 0; uc_reg_write(g_uc940, UC_ARM_REG_PC, &z);
    if (g_940_trace < 0) g_940_trace = getenv("ME_940_TRACE") ? 1 : 0;
    if (g_940_trace) { uc_hook th; uc_hook_add(g_uc940, &th, UC_HOOK_CODE, me940_trace_cb, NULL, 0, PRAM_SIZE - 1); }
    g_940_bank = bank; g_940_stop = 0; g_940_running = 1;
    if (pthread_create(&g_th940, NULL, me940_thread, NULL)) {
        fprintf(stderr, "[940] pthread_create failed\n"); g_940_running = 0; uc_close(g_uc940); g_uc940 = NULL; return;
    }
    fprintf(stderr, "[940] started: bank=%d, 940 addr 0 = phys %08x\n", bank, phys0);
}

void me940_stop(void) {
    if (!g_940_running) return;
    g_940_stop = 1;
    uc_emu_stop(g_uc940);
    pthread_join(g_th940, NULL);
    uc_close(g_uc940); g_uc940 = NULL;
    g_940_running = 0; g_940_bank = -1;
    if (g_940_trace > 0) {
        int buckets = 0; for (unsigned i = 0; i < sizeof g_940_seen; i++) buckets += g_940_seen[i];
        fprintf(stderr, "[940 trace] %ld insns, pc %08x..%08x, %d/64 code-KB buckets touched\n",
                g_940_insns, g_940_pcmin, g_940_pcmax, buckets);
    }
    fprintf(stderr, "[940] stopped\n");
}

/* Looks like a loadable ARM940 firmware sitting at phys 0x02000000? (first word = ARM `b` into the
   vector table, e.g. gpu940's 0xea000006). Guards against unrelated 0x3b48 pokes (Payback/vektar
   touch this register) spuriously spinning up a core over uninitialised RAM. */
static int firmware_present(void) {
    uint8_t *p = pram_host(PRAM_BASE);
    if (!p) return 0;
    uint32_t w0; memcpy(&w0, p, 4);
    return (w0 >> 24) == 0xeau;   /* ARM unconditional branch (reset vector) */
}

/* Trap of the 940 control registers from mmsp2_write_cb. Start on a halt->run transition with a
   firmware actually loaded; stop on run->halt. */
void me940_reg_write(uint32_t off, uint32_t val) {
    if (off == SYSCLKEN) { g_940_clock = val & 1; return; }
    if (off == DUALCTRL940) {
        int halt = (val >> 7) & 1, bank = val & 0x7f;
        if (!halt && !g_940_running && firmware_present()) me940_start(bank);
        else if (halt && g_940_running) me940_stop();
    }
}

/* Self-test ([940-2] verification, ME_940_SELFTEST=<gpu940 firmware path>): load the firmware into
   shared RAM and start the 940 directly (bypassing load940 + the launcher session), so the core can
   be exercised in isolation. Logs whether it advances past the CP15/MPU init into main. */
static uint32_t g_st_pcmin = 0xffffffff, g_st_pcmax = 0; static long g_st_insns = 0;
static void me940_st_code(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)uc; (void)size; (void)user;
    uint32_t pc = (uint32_t)addr;
    if (pc < g_st_pcmin) g_st_pcmin = pc;
    if (pc > g_st_pcmax) g_st_pcmax = pc;
    g_st_insns++;
}
/* Route present to gpu940's current DISPLAY buffer by reading its shared command queue (the proper
   gpu940 protocol, from include/gpu940.h). Shared struct base = phys 0x2100000:
     uint32_t cmds[0x40000-5]; cmds_begin; cmds_end; ...;  uint32_t buffers[] @ word 0x40000
   The displayed frame is the latest gpuSHOWBUF(=5) command: { opcode, buffer_loc{ address (words
   from buffers), width_log (log2 width), height } }. So the buffer is at phys 0x2200000+address*4
   with a (1<<width_log)*2-byte RGB565 stride -- that pow2 stride is why a 320px/640B present
   striped. Walk the ring back from cmds_end for the most recent valid SHOWBUF. */
#define GPU940_SHARED   0x2100000u
#define GPU940_BUFFERS  0x2200000u           /* shared + 0x40000 words */
#define GPU940_NCMDS    (0x40000u - 5u)
#define GPU_SHOWBUF     5u
uint32_t g_940_fb = 0;
void me940_scan_fb(void) {
    uint8_t *base = pram_host(GPU940_SHARED);
    if (!base) return;
    const uint32_t *cmds = (const uint32_t *)base;
    uint32_t cmds_end; memcpy(&cmds_end, base + (size_t)(0x40000 - 4) * 4, 4);
    if (cmds_end >= GPU940_NCMDS) cmds_end = 0;
    for (uint32_t s = 0; s < GPU940_NCMDS; s++) {                    /* walk back from the write head */
        uint32_t i = (cmds_end + GPU940_NCMDS - 1 - s) % GPU940_NCMDS;
        if (cmds[i] != GPU_SHOWBUF) continue;
        uint32_t a = cmds[(i + 1) % GPU940_NCMDS];
        uint32_t wl = cmds[(i + 2) % GPU940_NCMDS];
        uint32_t h = cmds[(i + 3) % GPU940_NCMDS];
        if (wl < 8 || wl > 12 || h < 16 || h > 1024) continue;       /* sanity: a real buffer_loc */
        uint32_t phys = GPU940_BUFFERS + a * 4, g;
        if (!phys_in_pram(phys, 320 * 240 * 2) || !phys_to_guest(phys, &g)) continue;
        g_940_fb = g; g_fb_guest = g; g_flip_active = 1; g_flip_guest = g;
        g_fb_bpp = 32;                                               /* gpu940 output is 32bpp XRGB */
        g_fb_stride = (1u << wl) * 4u;                               /* row = (1<<wl) 32-bit words */
        g_fb_xoff = (1u << wl) > 320 ? ((1u << wl) - 320) / 2 : 0;   /* 320 centered in the pow2 buffer */
        if (g_940_trace > 0)
            fprintf(stderr, "[940 showbuf] phys=%08x width=%u stride=%uB(32bpp) h=%u\n",
                    phys, 1u << wl, g_fb_stride, h);
        return;
    }
}

/* Emulate load940: place the gpu940 firmware blob into shared RAM at phys 0x02000000 and start the
   940 (bank 2). This is what the GP2X launcher's `load940 gpu940` step does; we do it inline so the
   940 is running before the client game (egoboo) starts, without relying on the program-reload path.
   Returns 1 if the 940 is running. */
int me940_load_and_start(const char *fw) {
    pram_ensure();
    uint8_t *dst = pram_host(PRAM_BASE);
    if (!dst) { fprintf(stderr, "[940] no shared RAM\n"); return 0; }
    FILE *f = fopen(fw, "rb");
    if (!f) { fprintf(stderr, "[940] cannot open firmware '%s'\n", fw); return 0; }
    size_t n = fread(dst, 1, PRAM_SIZE, f); fclose(f);
    uint32_t w0; memcpy(&w0, dst, 4);
    fprintf(stderr, "[940] firmware '%s' (%zu bytes) -> phys %08x; first word=%08x\n",
            fw, n, PRAM_BASE, w0);
    if ((w0 >> 24) != 0xeau) {   /* not a raw ARM reset vector (b ...) -- refuse to run garbage */
        fprintf(stderr, "[940] '%s' is not a raw ARM940 firmware (first word %08x); not starting\n", fw, w0);
        return 0;
    }
    me940_start(2);
    return g_940_running;
}

void me940_selftest(const char *fw) {
    fprintf(stderr, "[940 selftest] loading firmware '%s'\n", fw);
    if (!me940_load_and_start(fw)) { fprintf(stderr, "[940 selftest] FAILED to start\n"); return; }
    uc_hook h; uc_hook_add(g_uc940, &h, UC_HOOK_CODE, me940_st_code, NULL, 0, PRAM_SIZE - 1);
    struct timespec ts = { 0, 200 * 1000 * 1000 }; nanosleep(&ts, NULL);   /* let it run ~200ms */
    me940_stop();
    fprintf(stderr, "[940 selftest] executed %ld insns, pc range %08x..%08x (main is 0x6b8: %s)\n",
            g_st_insns, g_st_pcmin, g_st_pcmax, g_st_pcmax >= 0x6b8 ? "REACHED" : "not reached");
}
