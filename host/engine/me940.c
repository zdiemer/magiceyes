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

/* The 940's MMSP2 access (DUALINT cross-core signalling lives here). For now a benign page:
   reads return the stored halfword, writes are stored; full 920<->940 IPC is [940-4]. */
static void me940_mmio(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                       int64_t value, void *user) {
    (void)uc; (void)user;
    uint32_t off = (uint32_t)addr - MMSP2_PHYS;
    if (off + (uint32_t)size > MMSP2_LEN) return;
    if (type == UC_MEM_WRITE) memcpy(g_mmsp940 + off, &value, size);
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
void me940_selftest(const char *fw) {
    fprintf(stderr, "[940 selftest] loading firmware '%s'\n", fw);
    pram_ensure();                                 /* allocate the shared upper RAM (no 920 uc needed) */
    uint8_t *dst = pram_host(PRAM_BASE);
    FILE *f = fopen(fw, "rb");
    if (!f || !dst) { fprintf(stderr, "[940 selftest] cannot open firmware / no pram\n"); return; }
    size_t n = fread(dst, 1, PRAM_SIZE, f); fclose(f);
    fprintf(stderr, "[940 selftest] %zu bytes at phys %08x; first word=%08x\n",
            n, PRAM_BASE, *(uint32_t *)dst);
    me940_start(2);
    if (!g_940_running) { fprintf(stderr, "[940 selftest] FAILED to start\n"); return; }
    uc_hook h; uc_hook_add(g_uc940, &h, UC_HOOK_CODE, me940_st_code, NULL, 0, PRAM_SIZE - 1);
    struct timespec ts = { 0, 200 * 1000 * 1000 }; nanosleep(&ts, NULL);   /* let it run ~200ms */
    me940_stop();
    fprintf(stderr, "[940 selftest] executed %ld insns, pc range %08x..%08x (main is 0x6b8: %s)\n",
            g_st_insns, g_st_pcmin, g_st_pcmax, g_st_pcmax >= 0x6b8 ? "REACHED" : "not reached");
}
