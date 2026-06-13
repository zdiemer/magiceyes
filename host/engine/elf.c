/* magiceyes Unicorn engine — ELF loader: static ET_EXEC + dynamically-linked
 * (PT_INTERP) titles, plus the SysV stack (argc/argv/envp/auxv).
 *
 * Static GP2X games (GPEComp payloads: Payback, Blazar, Knight Lore, ...) are loaded
 * directly. Dynamic GP2X titles (Odonata, Wind & Water, RetroVirus) are EABI ET_EXEC that
 * link libSDL/libc and name `/lib/ld-linux.so.2` as their interpreter: we load BOTH the
 * program (at its fixed vaddrs) AND the interpreter (at a high base), hand the interpreter
 * an auxv pointing at the program's phdrs/entry, and start at the interpreter's entry — it
 * then opens + relocates the NEEDED libs itself via our syscall/mmap shim (the libs come
 * from the device rootfs; libSDL is shadowed by our fake-SDL shim). This mirrors qemu-user's
 * loader; the difference is we redirect the lib opens at the rootfs (see syscalls.c). */
#include "engine.h"

/* program-header info for the auxv (AT_PHDR/PHENT/PHNUM) — glibc's static-TLS setup
   reads the phdrs via AT_PHDR to find PT_TLS; without it, __thread/locale/stdio init is
   left half-built (fopen'd FILE*s get a null vtable). Set by load_elf. */
static uint32_t g_phdr_va, g_phnum, g_phent, g_elf_entry;
/* dynamic-link state for the auxv: AT_BASE = where the interpreter (ld.so) was loaded;
   0 for a static binary. AT_ENTRY = the program's entry (NOT the interpreter's). */
uint32_t g_at_base = 0;
int g_is_dynamic = 0;

/* The interpreter (ld-linux.so.2, ET_DYN) is loaded at this fixed base — above the mmap
   arena (0x40000000..0x70000000) and below the stack (0x80000000-8MB). ld.so + its bss are
   ~120KB; the NEEDED libs are mmap'd into the arena by ld.so itself. */
#define INTERP_BASE 0x71000000u

/* Map every PT_LOAD of an ELF image already read into `buf` at `bias`, returning the highest
   guest end address. BSS (memsz>filesz) reads as zero because map_region hands out fresh
   zeroed anon host memory and we only write the filesz bytes. Perms are relaxed to RWX (ld.so
   mprotects later, which the engine no-ops). */
static uint32_t map_loads(uint8_t *buf, Elf32_Ehdr *eh, uint32_t bias) {
    uint32_t max_end = 0;
    Elf32_Phdr *ph = (Elf32_Phdr *)(buf + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        uint32_t va = bias + ph[i].p_vaddr, fsz = ph[i].p_filesz, msz = ph[i].p_memsz;
        map_region(va, msz, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
        if (fsz) {
            uc_err e = uc_mem_write(g_uc, va, buf + ph[i].p_offset, fsz);
            if (e) die("uc_mem_write seg", e);
        }
        if (va + msz > max_end) max_end = va + msz;
        if (g_trace) fprintf(stderr, "  PT_LOAD va=%08x filesz=%u memsz=%u (bias=%08x)\n",
                             va, fsz, msz, bias);
    }
    return max_end;
}

/* Slurp a file into a malloc'd buffer (caller frees). Returns NULL+message on error. */
static uint8_t *slurp(const char *path, long *out_sz) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "magiceyes: cannot open '%s': %s\n", path, strerror(errno)); return NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fprintf(stderr, "magiceyes: '%s' is empty/invalid\n", path); fclose(f); return NULL; }
    uint8_t *buf = malloc(sz);
    if (!buf || fread(buf, 1, sz, f) != (size_t)sz) {
        fprintf(stderr, "magiceyes: read '%s' failed\n", path); free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_sz = sz;
    return buf;
}

static int is_arm_elf(uint8_t *buf, long sz) {
    Elf32_Ehdr *eh = (Elf32_Ehdr *)buf;
    return (size_t)sz >= sizeof *eh && !memcmp(eh->e_ident, ELFMAG, SELFMAG) &&
           eh->e_ident[EI_CLASS] == ELFCLASS32 && eh->e_machine == EM_ARM;
}

/* Load ld-linux.so.2 (the PT_INTERP) from the device rootfs at INTERP_BASE; returns its
   entry PC, or 0 on failure. */
static uint32_t load_interp(const char *guest_interp) {
    char host[PATH_MAX];
    if (!me_rootfs_resolve(guest_interp, host, sizeof host)) {
        fprintf(stderr, "magiceyes: interpreter '%s' not found in the device rootfs "
                        "(set ME_GP2X_ROOTFS; see host/win/stage_rootfs.sh)\n", guest_interp);
        return 0;
    }
    long sz; uint8_t *buf = slurp(host, &sz);
    if (!buf) return 0;
    if (!is_arm_elf(buf, sz)) { fprintf(stderr, "magiceyes: interp '%s' not ARM ELF\n", host); free(buf); return 0; }
    Elf32_Ehdr *eh = (Elf32_Ehdr *)buf;
    map_loads(buf, eh, INTERP_BASE);
    uint32_t entry = INTERP_BASE + eh->e_entry;
    if (g_trace) fprintf(stderr, "  interp %s -> base=%08x entry=%08x\n", host, INTERP_BASE, entry);
    free(buf);
    return entry;
}

/* ---- ELF loader (static EXEC or dynamic ET_EXEC w/ PT_INTERP) ---- */
/* Returns the entry PC (the interpreter's, for a dynamic binary), or 0 on a recoverable
   failure (bad path/format) so the caller can go idle instead of killing the GUI process. */
int g_device = 0;       /* viewer-header device, set in load_elf: enum me_device (index into me_model()) */

/* case-insensitive equality (MinGW lacks a portable strcasecmp in <string.h> here). */
static int eq_ci(const char *a, const char *b) {
    for (; *a && *b; a++, b++) { int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0; }
    return *a == *b;
}

/* substring search over a byte buffer (portable; MinGW has no memmem). */
static int buf_has(const uint8_t *buf, long sz, const char *s) {
    long n = (long)strlen(s);
    for (long i = 0; i + n <= sz; i++)
        if (buf[i] == (uint8_t)s[0] && memcmp(buf + i, s, (size_t)n) == 0) return 1;
    return 0;
}

uint32_t load_elf(const char *path) {
    long sz; uint8_t *buf = slurp(path, &sz);
    if (!buf) return 0;
    if (!is_arm_elf(buf, sz)) { fprintf(stderr, "magiceyes: '%s' is not a 32-bit ARM ELF\n", path); free(buf); return 0; }
    Elf32_Ehdr *eh = (Elf32_Ehdr *)buf;
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) {
        fprintf(stderr, "magiceyes: '%s' is not an executable ELF (e_type=%u)\n", path, eh->e_type); free(buf); return 0; }

    /* find the program interpreter, if any */
    char interp[256] = {0};
    Elf32_Phdr *ph = (Elf32_Phdr *)(buf + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_INTERP && ph[i].p_filesz < sizeof interp)
            memcpy(interp, buf + ph[i].p_offset, ph[i].p_filesz);

    /* ---- Device classification (viewer header + the shim's per-device input map) ----
       Worked out by probing 44 real ROMs (tools/probe_elf.py + probe_strings.py). The key
       finding: the ELF *toolchain generation* does NOT map to the device. Two examples that
       break the obvious heuristics:
         - the EABI Wiz title Patissier (rg_ura) is byte-identical to a Caanoo binary
           (ld-linux.so.3 / EABI v4 / OSABI=SysV), and
         - old-toolchain Wiz titles (Cave Story, Deicide, Her Knights) are indistinguishable
           from GP2X (ld-linux.so.2 / EABI ver 0 / OSABI=ARM-EABI).
       So we use only signals that are actually reliable, and -- per the project decision --
       default the unavoidable GP2X<->Wiz ambiguity to GP2X. An explicit MAGICEYES_DEVICE
       always wins. Priority:
         1. explicit MAGICEYES_DEVICE env;
         2. static binary (no PT_INTERP) -> GP2X (every Wiz/Caanoo title in the wild is dynamic;
            GP2X's static GPEComp payloads are the only static .gpe);
         3. a positive Caanoo marker (Pollux SoC sonames/device nodes, or a _Pollux filename)
            -> Caanoo (these never appear on GP2X or Wiz);
         4. a positive Wiz marker (the GPH Wiz SDL extensions / the Wiz toolkit lib) -> Wiz;
         5. interp is ld-linux.so.3 -> Caanoo. GP2X firmware is ld.so.2-ONLY, so an so.3 binary
            is never GP2X; Caanoo is the dominant so.3 device. This also catches Caanoo titles
            with no Pollux soname (e.g. Liar: Redemption -- plain SDL, but needs Caanoo input).
            The rare so.3 Wiz title (Patissier) lands here too; override with MAGICEYES_DEVICE=wiz.
         6. otherwise (ld.so.2, no marker) -> GP2X (the unavoidable GP2X<->Wiz ambiguity). */
    int dev = -1;
    const char *envdev = getenv("MAGICEYES_DEVICE");
    if (envdev) {
        if (eq_ci(envdev, "caanoo")) dev = 2;
        else if (eq_ci(envdev, "wiz")) dev = 1;
        else if (eq_ci(envdev, "gp2x") || eq_ci(envdev, "f100") || eq_ci(envdev, "f200")) dev = 0;
        else if (eq_ci(envdev, "didj")) dev = 3;
    }
    if (dev < 0 && !interp[0]) dev = 0;   /* static ELF -> GP2X (Didj titles are all dynamic) */
    if (dev < 0) {
        /* Didj (LeapFrog LF1000): the uClibc dynamic linker is unique to Didj among supported
           devices, and its games/launcher link the LeapFrog "MPI" HAL libs. Either signal -> Didj.
           Checked before the Caanoo/Wiz heuristics since those assume a glibc ld-linux interp. */
        int didj = strstr(interp, "ld-uClibc.so.0") != NULL;
        if (!didj) {
            static const char *didj_sig[] = { "libDisplayMPI", "libAudioMPI", "libButtonMPI",
                                              "libKernelMPI", "libLightningBase" };
            for (unsigned k = 0; k < sizeof didj_sig / sizeof didj_sig[0]; k++)
                if (buf_has(buf, sz, didj_sig[k])) { didj = 1; break; }
        }
        if (didj) dev = 3;
    }
    if (dev < 0) {
        /* Pollux/Caanoo-only sonames + device nodes (probed: present on no GP2X/Wiz title). */
        static const char *caanoo_sig[] = {
            "libopengles_lite", "libGLESv1_CM", "libOpenEGL", "libglport", "libMesNativeOEM",
            "libDrv.so", "libmedia.so", "librec.so", "libdge20.so", "libdgt20.so", "libdgx20.so",
            "/dev/pollux", "pollux_clock", "/dev/isa1200" };
        int caanoo = 0;
        for (unsigned k = 0; k < sizeof caanoo_sig / sizeof caanoo_sig[0]; k++)
            if (buf_has(buf, sz, caanoo_sig[k])) { caanoo = 1; break; }
        if (!caanoo && (buf_has(buf, sz, "pollux") || buf_has(buf, sz, "Pollux"))) caanoo = 1;
        if (!caanoo && (strstr(path, "_Pollux") || strstr(path, "_pollux"))) caanoo = 1;
        /* Wiz-only markers: the GPH Wiz SDL extensions (Her Knights) + the Wiz toolkit lib. */
        static const char *wiz_sig[] = { "libtngp2xtk.so", "SDL_SetLcdMode", "SetLcdMode",
                                         "SDL_TvConfig" };
        int wiz = 0;
        for (unsigned k = 0; k < sizeof wiz_sig / sizeof wiz_sig[0]; k++)
            if (buf_has(buf, sz, wiz_sig[k])) { wiz = 1; break; }
        /* ld-linux.so.3 = the newer CodeSourcery/GPH toolchain; GP2X never shipped it, so an
           so.3 binary is Caanoo (or the rare so.3 Wiz title -> override). Catches Liar et al. */
        int so3 = strstr(interp, "ld-linux.so.3") != NULL;
        dev = caanoo ? 2 : (wiz ? 1 : (so3 ? 2 : 0));
    }
    g_device = dev;
    coop_init(dev == ME_DEV_DIDJ);   /* single-core run token for Didj (uClibc not parallel-safe) */
    if (g_trace) fprintf(stderr, "  device=%d (%s) interp=%s\n", dev,
                         me_model()->name, interp[0] ? interp : "(static)");

    /* The program loads at its fixed vaddrs (ET_EXEC, bias 0). PIE (ET_DYN main) isn't a GP2X
       case, so we don't relocate the main image. */
    uint32_t max_end = map_loads(buf, eh, 0);
    g_brk_start = g_brk = ALIGN_UP(max_end);
    map_region(g_brk, PAGE, UC_PROT_READ | UC_PROT_WRITE);  /* initial brk page */

    /* AT_PHDR: where the phdrs landed in guest memory (the PT_LOAD covering e_phoff). */
    g_phdr_va = 0;
    for (int i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_LOAD && eh->e_phoff >= ph[i].p_offset &&
            eh->e_phoff < ph[i].p_offset + ph[i].p_filesz) {
            g_phdr_va = ph[i].p_vaddr + (eh->e_phoff - ph[i].p_offset); break;
        }
    g_phnum = eh->e_phnum; g_phent = eh->e_phentsize; g_elf_entry = eh->e_entry;

    uint32_t entry = eh->e_entry;
    if (interp[0]) {                         /* dynamic: load + start the interpreter */
        me_rootfs_select(interp);            /* so.2 firmware vs so.3 EABI rootfs */
        uint32_t ie = load_interp(interp);
        if (!ie) { free(buf); return 0; }
        g_at_base = ie ? INTERP_BASE : 0;
        g_is_dynamic = 1;
        entry = ie;                          /* start in ld.so; it jumps to the program */
        if (g_trace) fprintf(stderr, "  dynamic: prog entry=%08x phdr=%08x interp_base=%08x\n",
                             g_elf_entry, g_phdr_va, g_at_base);
    } else {
        g_at_base = 0; g_is_dynamic = 0;
    }
    free(buf);
    return entry;
}

/* ---- stack: argc, argv[], NULL, envp[], NULL, auxv[], NULL ---- */
/* For dynamic binaries we also push a small envp (LD_LIBRARY_PATH so the rootfs libs resolve,
   FAKESDL_FPS for the shim's frame cap) — a static GP2X game ignores env, so it's harmless. */
uint32_t setup_stack(int argc, char **argv) {
    map_region(STACK_TOP - STACK_SIZE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    uint32_t sp = STACK_TOP;

    /* env strings. A baseline set is pushed for EVERY title (not just dynamic ones): static GP2X
       games otherwise get an empty environment, and a getenv() that returns NULL fed straight into
       a std::string ctor aborts before the first frame (openglad2x: "basic_string::_S_construct
       NULL not valid"). These mirror what the GP2X firmware exports; harmless to games that ignore
       env. */
    const char *envs[28]; int nenv = 0;
    static char envbuf[28][128];
    envs[nenv++] = "HOME=/tmp";
    envs[nenv++] = "PWD=.";
    envs[nenv++] = "TERM=linux";
    envs[nenv++] = "USER=root";
    envs[nenv++] = "LOGNAME=root";
    envs[nenv++] = "LANG=C";
    envs[nenv++] = "TMPDIR=/tmp";
    if (g_is_dynamic) {
        envs[nenv++] = "LD_LIBRARY_PATH=/lib:/usr/lib";
        const char *f = getenv("ME_GP2X_FPS");
        snprintf(envbuf[nenv], sizeof envbuf[0], "FAKESDL_FPS=%s", f ? f : "60"); envs[nenv] = envbuf[nenv]; nenv++;
        /* device profile for the shim's per-device joystick map (GP2X/Wiz default vs Caanoo's
           analog-stick-axes + native button order). Explicit MAGICEYES_DEVICE wins; else use the
           auto-detected device's guest tag (me_model()->guest_env; NULL for GP2X/Wiz). */
        const char *dev = getenv("MAGICEYES_DEVICE");
        if (!dev) dev = me_model()->guest_env;
        if (dev && nenv < 11) { snprintf(envbuf[nenv], sizeof envbuf[0], "MAGICEYES_DEVICE=%s", dev);
                                envs[nenv] = envbuf[nenv]; nenv++; }
        /* Forward shim debug toggles from the host env (the guest getenv reads only the envp we
           build here, not the host environment): ME_FAKESDL_FOO -> FAKESDL_FOO in the guest. */
        static const char *fwd[] = { "FAKESDL_BLIT_LOG", "FAKESDL_PRESENT_LOG", "FAKESDL_NO_COLORKEY",
                                     "FAKESDL_SRC_DUMP", "FAKESDL_AUDIO_DUMP", "FAKEGLES_LOG",
                                     "FAKEGLES_NORAST", "FAKESDL_VTIME", "FAKESDL_FBLOG",
                                     "FAKESDL_TIDLOG" };
        for (int i = 0; i < (int)(sizeof fwd / sizeof fwd[0]) && nenv < 26; i++) {
            char host[64]; snprintf(host, sizeof host, "ME_%s", fwd[i]);
            const char *v = getenv(host);
            if (v) { snprintf(envbuf[nenv], sizeof envbuf[0], "%s=%s", fwd[i], v); envs[nenv] = envbuf[nenv]; nenv++; }
        }
        /* glibc malloc tuning, forwarded verbatim from the host env (no ME_ prefix): lets us pin
           MALLOC_ARENA_MAX / turn on MALLOC_CHECK_ to diagnose (and work around) heap corruption in
           multi-threaded titles without rebuilding. */
        static const char *mfwd[] = { "MALLOC_ARENA_MAX", "MALLOC_CHECK_", "MALLOC_PERTURB_" };
        for (int i = 0; i < (int)(sizeof mfwd / sizeof mfwd[0]) && nenv < 27; i++) {
            const char *v = getenv(mfwd[i]);
            if (v) { snprintf(envbuf[nenv], sizeof envbuf[0], "%s=%s", mfwd[i], v); envs[nenv] = envbuf[nenv]; nenv++; }
        }
        /* Tell the shim to emit structured run-report lines (the engine ingests them off stderr).
           Off by default so a normal play session does no per-frame report work in the guest. */
        if ((getenv("ME_DEBUG") || getenv("ME_REPORT")) && nenv < 27) {
            snprintf(envbuf[nenv], sizeof envbuf[0], "ME_DEBUG=1"); envs[nenv] = envbuf[nenv]; nenv++;
        }
    }
    uint32_t envp[28];

    /* push strings, collect guest pointers */
    uint32_t argp[64]; int n = argc < 63 ? argc : 63;
    for (int i = n - 1; i >= 0; i--) {
        size_t l = strlen(argv[i]) + 1;
        sp -= l; sp &= ~3u;
        uc_mem_write(g_uc, sp, argv[i], l);
        argp[i] = sp;
    }
    for (int i = nenv - 1; i >= 0; i--) {
        size_t l = strlen(envs[i]) + 1;
        sp -= l; sp &= ~3u;
        uc_mem_write(g_uc, sp, envs[i], l);
        envp[i] = sp;
    }
    /* 16 random bytes for AT_RANDOM */
    sp -= 16; sp &= ~15u; uint32_t at_random = sp;
    uint8_t rnd[16] = {0x4d,0x61,0x67,0x69,0x63,0x45,0x79,0x65,0x73,1,2,3,4,5,6,7};
    uc_mem_write(g_uc, sp, rnd, 16);
    /* AT_PLATFORM string */
    sp -= 4; sp &= ~3u; uint32_t at_platform = sp;
    uc_mem_write(g_uc, sp, "v5l", 4);

    /* Full auxv, mirroring qemu-user's create_elf_tables() so glibc's static-TLS +
       stdio/C++ init complete (AT_PHDR is the critical one for PT_TLS; AT_BASE/AT_ENTRY
       drive the dynamic linker). */
    uint32_t aux[][2] = {
        {3 /*AT_PHDR*/,    g_phdr_va},
        {4 /*AT_PHENT*/,   g_phent},
        {5 /*AT_PHNUM*/,   g_phnum},
        {6 /*AT_PAGESZ*/,  PAGE},
        {7 /*AT_BASE*/,    g_at_base},
        {8 /*AT_FLAGS*/,   0},
        {9 /*AT_ENTRY*/,   g_elf_entry},
        {11/*AT_UID*/,     0}, {12/*AT_EUID*/, 0},
        {13/*AT_GID*/,     0}, {14/*AT_EGID*/, 0},
        {16/*AT_HWCAP*/,   0x97},   /* armv5te: SWP|HALF|THUMB|FAST_MULT|EDSP */
        {17/*AT_CLKTCK*/,  100},
        {15/*AT_PLATFORM*/,at_platform},
        {23/*AT_SECURE*/,  0},
        {25/*AT_RANDOM*/,  at_random},
        {31/*AT_EXECFN*/,  n ? argp[0] : 0},
        {0 /*AT_NULL*/,    0},
    };
    int naux = sizeof(aux) / sizeof(aux[0]);
    /* total words: argc(1) + argv(n) + null(1) + envp(nenv) + null(1) + aux(2*naux) */
    int words = 1 + n + 1 + nenv + 1 + 2 * naux;
    uint32_t block = sp - words * 4;
    block &= ~7u;
    uint32_t p = block;
    uint32_t w;
    w = n;        uc_mem_write(g_uc, p, &w, 4); p += 4;
    for (int i = 0; i < n; i++) { uc_mem_write(g_uc, p, &argp[i], 4); p += 4; }
    w = 0;        uc_mem_write(g_uc, p, &w, 4); p += 4;   /* argv NULL */
    for (int i = 0; i < nenv; i++) { uc_mem_write(g_uc, p, &envp[i], 4); p += 4; }
    w = 0;        uc_mem_write(g_uc, p, &w, 4); p += 4;   /* envp NULL */
    for (int i = 0; i < naux; i++) {
        uc_mem_write(g_uc, p, &aux[i][0], 4); p += 4;
        uc_mem_write(g_uc, p, &aux[i][1], 4); p += 4;
    }
    return block;
}
