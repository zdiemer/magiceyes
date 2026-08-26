/* magiceyes — OABI libm float-return shim.
 *
 * GP2X binaries use the legacy ARM OABI/APCS float ABI: `double` ARGUMENTS are passed in the core
 * register pairs (r0/r1, r2/r3) with the FPA word order (high 32-bit word in the lower register),
 * but a `double` RESULT is returned in FPA register **f0**. Our staged rootfs libm.so.6 is
 * soft-float and returns doubles in r0/r1 -- so an OABI game that reads its `cos()`/`sin()` result
 * from f0 gets whatever stale value was in f0 (garbage). Odonata's startup builds a cos/sin lookup
 * table this way; with the wrong table, bullet velocities collapse to ~0 and they crawl.
 *
 * Rather than depend on a matching FPA libm (none is staged; all our libm builds are soft-float),
 * we intercept the GAME's PLT stub for each double-returning libm function, compute it natively on
 * the host, and return the result BOTH in f0 (the OABI convention the game reads) and in r0/r1
 * (harmless, and keeps a soft-float-return caller working too). Hooking the PLT stub (bias-0, fixed
 * address, executed on every call before the GOT indirection) makes lazy binding irrelevant.
 *
 * Mechanism: overwrite each stub's first instruction with `bx lr` so the call returns immediately
 * (libm never runs, so it can't clobber f0), and register a UC_HOOK_CODE at the stub that only
 * SETS registers (f0/r0/r1) before the `bx lr` executes. We deliberately do NOT rewrite PC in the
 * hook (this Unicorn fork doesn't honor that) and we do NOT fault/stop -- so there's no per-call
 * uc_emu_start restart, keeping hot callers (e.g. the Lua VM's floor()) fast.
 *
 * Scoped to GP2X (OABI) titles only: EABI (Wiz/Caanoo) titles pass doubles in AAPCS order
 * (r0 = LOW word) and read the result from r0/r1, so both the arg decode and the need differ --
 * enabling this there would mis-decode arguments. See caller in elf.c (guarded on g_device==0).
 */
#include "engine.h"
#include "armfp.h"
#include <math.h>
#include <string.h>

struct fn { const char *name; int argc; };
static const struct fn FNS[] = {
    {"cos",1}, {"sin",1}, {"tan",1}, {"floor",1}, {"ceil",1}, {"sqrt",1}, {"fabs",1},
    {"exp",1}, {"log",1}, {"log10",1}, {"asin",1}, {"acos",1}, {"atan",1},
    {"atan2",2}, {"pow",2}, {"fmod",2}, {"hypot",2},
};
#define NFN ((int)(sizeof FNS / sizeof FNS[0]))

static struct { uint32_t addr; int fn; } g_shim[32];
static int g_nshim = 0;

/* Read a double from an OABI register pair: reg_hi holds the HIGH 32-bit word (FPA order, as the
   game marshals via stfd->pop), reg_lo the low word. Verified against a live capture
   (0x3faacee9_f37c4b99 = 0.05235988 = pi/60). */
static double rd_double(uint32_t reg_hi, uint32_t reg_lo) {
    return fpa_words_to_double(gread(reg_hi), gread(reg_lo));
}

/* UC_HOOK_CODE at a (bx-lr-patched) shim stub: compute natively, put the result in f0 (+ r0/r1).
   We do NOT touch PC -- the patched `bx lr` at this address does the return on its own. */
static void oabi_libm_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)uc; (void)size; (void)user;
    int fn = -1;
    for (int i = 0; i < g_nshim; i++) if (g_shim[i].addr == (uint32_t)addr) { fn = g_shim[i].fn; break; }
    if (fn < 0) return;

    double a = rd_double(UC_ARM_REG_R0, UC_ARM_REG_R1);
    double b = FNS[fn].argc == 2 ? rd_double(UC_ARM_REG_R2, UC_ARM_REG_R3) : 0.0;
    double r = oabi_libm_compute(fn, a, b);

    fpa_write(0, r);                                    /* OABI: double result in FPA f0 */
    uint64_t u; memcpy(&u, &r, 8);                      /* also r0/r1 (r0=high word, r1=low word) */
    gwrite(UC_ARM_REG_R0, (uint32_t)(u >> 32));
    gwrite(UC_ARM_REG_R1, (uint32_t)u);
}

/* Parse the just-loaded game ELF image (bias 0) and record the PLT stub address of every
   double-returning libm function it imports. ARM PLT: 20-byte header + 12-byte entries, in the
   same order as .rel.plt, so stub[N] = .plt + 20 + N*12. */
void oabi_libm_scan(const uint8_t *buf, long sz) {
    g_nshim = 0;
    if (!buf || sz < (long)sizeof(Elf32_Ehdr)) return;
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)buf;
    if (!eh->e_shoff || !eh->e_shnum || eh->e_shstrndx >= eh->e_shnum) return;
    if (eh->e_shoff + (long)eh->e_shnum * eh->e_shentsize > sz) return;
    const Elf32_Shdr *sh = (const Elf32_Shdr *)(buf + eh->e_shoff);
    const char *shstr = (const char *)(buf + sh[eh->e_shstrndx].sh_offset);

    uint32_t plt_addr = 0;
    const Elf32_Rel *relplt = NULL; uint32_t nrel = 0;
    const Elf32_Sym *dynsym = NULL; const char *dynstr = NULL;
    for (int i = 0; i < eh->e_shnum; i++) {
        const char *nm = shstr + sh[i].sh_name;
        if      (!strcmp(nm, ".plt"))     plt_addr = sh[i].sh_addr;
        else if (!strcmp(nm, ".rel.plt")) { relplt = (const Elf32_Rel *)(buf + sh[i].sh_offset);
                                            nrel = sh[i].sh_entsize ? sh[i].sh_size / sh[i].sh_entsize : 0; }
        else if (!strcmp(nm, ".dynsym"))  dynsym = (const Elf32_Sym *)(buf + sh[i].sh_offset);
        else if (!strcmp(nm, ".dynstr"))  dynstr = (const char *)(buf + sh[i].sh_offset);
    }
    if (!plt_addr || !relplt || !dynsym || !dynstr) return;

    for (uint32_t n = 0; n < nrel && g_nshim < (int)(sizeof g_shim / sizeof g_shim[0]); n++) {
        const char *nm = dynstr + dynsym[ELF32_R_SYM(relplt[n].r_info)].st_name;
        for (int f = 0; f < NFN; f++) if (!strcmp(nm, FNS[f].name)) {
            g_shim[g_nshim].addr = plt_addr + 20 + n * 12;
            g_shim[g_nshim].fn = f;
            g_nshim++;
            if (g_trace) fprintf(stderr, "  [oabi-libm] shim %-6s -> plt stub %08x\n", nm, plt_addr + 20 + n * 12);
            break;
        }
    }
    if (g_nshim && g_trace) fprintf(stderr, "  [oabi-libm] %d double-returning libm function(s) shimmed\n", g_nshim);
}

/* Overwrite each shimmed PLT stub's first instruction with `bx lr` (once; guest RAM is shared and
   this runs before the game executes) and register the register-setting hook on this thread's uc. */
void oabi_libm_register(uc_engine *u) {
    static int patched = 0;
    if (!patched && g_nshim) {
        patched = 1;
        uint32_t bxlr = 0xe12fff1eu;                    /* bx lr */
        for (int i = 0; i < g_nshim; i++) uc_mem_write(u, g_shim[i].addr, &bxlr, 4);
        if (g_trace) fprintf(stderr, "  [oabi-libm] patched %d PLT stub(s) -> bx lr + native f0-return\n", g_nshim);
    }
    for (int i = 0; i < g_nshim; i++) {
        uc_hook h;
        uc_hook_add(u, &h, UC_HOOK_CODE, oabi_libm_cb, NULL, g_shim[i].addr, g_shim[i].addr);
    }
}
