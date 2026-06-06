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
int g_caanoo_dev = 0;   /* set in load_elf: binary links Pollux/Caanoo GLES libs -> Caanoo device */

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

    /* Caanoo auto-detect (for the shim's per-device joystick map): Caanoo .gpe link Pollux
       GLES/MES/media libs that no Wiz/GP2X title uses. Zero false positives on Wiz; titles
       without these (e.g. Liar) need MAGICEYES_DEVICE=caanoo set explicitly. TODO: full
       per-device profiles + remappable bindings (the user-flagged TODO). */
    g_caanoo_dev = 0;
    { static const char *sig[] = {
          /* Pollux GLES/MES/media libs (Propis, Rhythmos) */
          "libopengles_lite", "libGLESv1_CM", "libOpenEGL", "libglport",
          "libMesNativeOEM", "libDrv.so", "libmedia.so", "librec.so", "libunicodefont",
          /* the Caanoo "DGE" game engine (Propis, Rhythmos) + the Caanoo OSS audio dir (Liar);
             Wiz/GP2X titles use /dev/dsp, not /dev/sound, and never link DGE. */
          "DGE_Display", "/dev/sound/" };
      for (unsigned k = 0; k < sizeof sig / sizeof sig[0]; k++)
          if (buf_has(buf, sz, sig[k])) { g_caanoo_dev = 1; break; } }

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

    /* env strings (dynamic only) */
    const char *envs[12]; int nenv = 0;
    static char envbuf[12][128];
    if (g_is_dynamic) {
        envs[nenv++] = "LD_LIBRARY_PATH=/lib:/usr/lib";
        envs[nenv++] = "HOME=/tmp";
        const char *f = getenv("ME_GP2X_FPS");
        snprintf(envbuf[nenv], sizeof envbuf[0], "FAKESDL_FPS=%s", f ? f : "60"); envs[nenv] = envbuf[nenv]; nenv++;
        /* device profile for the shim's per-device joystick map (GP2X/Wiz default vs Caanoo's
           analog-stick-axes + native button order). Explicit MAGICEYES_DEVICE wins; else use the
           auto-detected device (g_caanoo_dev from the binary's sonames in load_elf). */
        const char *dev = getenv("MAGICEYES_DEVICE");
        if (!dev && g_caanoo_dev) dev = "caanoo";
        if (dev && nenv < 11) { snprintf(envbuf[nenv], sizeof envbuf[0], "MAGICEYES_DEVICE=%s", dev);
                                envs[nenv] = envbuf[nenv]; nenv++; }
        /* Forward shim debug toggles from the host env (the guest getenv reads only the envp we
           build here, not the host environment): ME_FAKESDL_FOO -> FAKESDL_FOO in the guest. */
        static const char *fwd[] = { "FAKESDL_BLIT_LOG", "FAKESDL_PRESENT_LOG", "FAKESDL_NO_COLORKEY",
                                     "FAKESDL_SRC_DUMP", "FAKESDL_AUDIO_DUMP", "FAKEGLES_LOG",
                                     "FAKEGLES_NORAST" };
        for (int i = 0; i < (int)(sizeof fwd / sizeof fwd[0]) && nenv < 11; i++) {
            char host[64]; snprintf(host, sizeof host, "ME_%s", fwd[i]);
            const char *v = getenv(host);
            if (v) { snprintf(envbuf[nenv], sizeof envbuf[0], "%s=%s", fwd[i], v); envs[nenv] = envbuf[nenv]; nenv++; }
        }
    }
    uint32_t envp[12];

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
