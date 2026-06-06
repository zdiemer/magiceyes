/* magiceyes Unicorn engine — ELF loader (static ET_EXEC) + SysV stack setup.
 * (A dynamic-linker / PT_INTERP path for Wiz titles lands here later.) */
#include "engine.h"

/* program-header info for the auxv (AT_PHDR/PHENT/PHNUM) — glibc's static-TLS setup
   reads the phdrs via AT_PHDR to find PT_TLS; without it, __thread/locale/stdio init is
   left half-built (fopen'd FILE*s get a null vtable). Set by load_elf. */
static uint32_t g_phdr_va, g_phnum, g_phent, g_elf_entry;

/* ---- ELF loader (static EXEC) ---- */
/* Returns the entry PC, or 0 on a recoverable failure (bad path/format) so the caller can return
   to an idle window instead of killing the process -- a game's execve to a missing/garbage path
   must NOT take the whole GUI down. */
uint32_t load_elf(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "magiceyes: cannot open '%s': %s\n", path, strerror(errno)); return 0; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fprintf(stderr, "magiceyes: '%s' is empty/invalid\n", path); fclose(f); return 0; }
    uint8_t *buf = malloc(sz);
    if (!buf || fread(buf, 1, sz, f) != (size_t)sz) { fprintf(stderr, "magiceyes: read '%s' failed\n", path); free(buf); fclose(f); return 0; }
    fclose(f);

    Elf32_Ehdr *eh = (Elf32_Ehdr *)buf;
    if ((size_t)sz < sizeof *eh ||
        memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32)
        { fprintf(stderr, "magiceyes: '%s' is not a 32-bit ELF\n", path); free(buf); return 0; }
    if (eh->e_machine != EM_ARM) { fprintf(stderr, "magiceyes: '%s' is not ARM\n", path); free(buf); return 0; }
    if (eh->e_type != ET_EXEC) { fprintf(stderr, "magiceyes: '%s' is not a static ET_EXEC\n", path); free(buf); return 0; }

    uint32_t max_end = 0;
    Elf32_Phdr *ph = (Elf32_Phdr *)(buf + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        uint32_t va = ph[i].p_vaddr, fsz = ph[i].p_filesz, msz = ph[i].p_memsz;
        uint32_t perms = UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC; /* relax for now */
        map_region(va, msz, perms);
        if (fsz) {
            uc_err e = uc_mem_write(g_uc, va, buf + ph[i].p_offset, fsz);
            if (e) die("uc_mem_write seg", e);
        }
        if (va + msz > max_end) max_end = va + msz;
        if (g_trace) fprintf(stderr, "  PT_LOAD va=%08x filesz=%u memsz=%u\n", va, fsz, msz);
    }
    g_brk_start = g_brk = ALIGN_UP(max_end);
    map_region(g_brk, PAGE, UC_PROT_READ | UC_PROT_WRITE); /* initial brk page */
    /* where the phdrs ended up in guest memory (the PT_LOAD covering e_phoff) */
    g_phdr_va = 0;
    for (int i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_LOAD && eh->e_phoff >= ph[i].p_offset &&
            eh->e_phoff < ph[i].p_offset + ph[i].p_filesz) {
            g_phdr_va = ph[i].p_vaddr + (eh->e_phoff - ph[i].p_offset); break;
        }
    g_phnum = eh->e_phnum; g_phent = eh->e_phentsize; g_elf_entry = eh->e_entry;
    uint32_t entry = eh->e_entry;
    free(buf);
    return entry;
}

/* ---- stack: argc, argv[], NULL, envp[], NULL, auxv[], NULL ---- */
uint32_t setup_stack(int argc, char **argv) {
    map_region(STACK_TOP - STACK_SIZE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    uint32_t sp = STACK_TOP;

    /* push strings, collect guest pointers */
    uint32_t argp[64]; int n = argc < 63 ? argc : 63;
    for (int i = n - 1; i >= 0; i--) {
        size_t l = strlen(argv[i]) + 1;
        sp -= l; sp &= ~3u;
        uc_mem_write(g_uc, sp, argv[i], l);
        argp[i] = sp;
    }
    /* 16 random bytes for AT_RANDOM */
    sp -= 16; sp &= ~15u; uint32_t at_random = sp;
    uint8_t rnd[16] = {0x4d,0x61,0x67,0x69,0x63,0x45,0x79,0x65,0x73,1,2,3,4,5,6,7};
    uc_mem_write(g_uc, sp, rnd, 16);
    /* AT_PLATFORM string */
    sp -= 4; sp &= ~3u; uint32_t at_platform = sp;
    uc_mem_write(g_uc, sp, "v5l", 4);

    /* Full auxv, mirroring qemu-user's create_elf_tables() so glibc's static-TLS +
       stdio/C++ init complete (AT_PHDR is the critical one for PT_TLS). */
    uint32_t aux[][2] = {
        {3 /*AT_PHDR*/,    g_phdr_va},
        {4 /*AT_PHENT*/,   g_phent},
        {5 /*AT_PHNUM*/,   g_phnum},
        {6 /*AT_PAGESZ*/,  PAGE},
        {7 /*AT_BASE*/,    0},
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
    /* total words: argc(1) + argv(n) + null(1) + envp null(1) + aux(2*naux) */
    int words = 1 + n + 1 + 1 + 2 * naux;
    uint32_t block = sp - words * 4;
    block &= ~7u;
    uint32_t p = block;
    uint32_t w;
    w = n;        uc_mem_write(g_uc, p, &w, 4); p += 4;
    for (int i = 0; i < n; i++) { uc_mem_write(g_uc, p, &argp[i], 4); p += 4; }
    w = 0;        uc_mem_write(g_uc, p, &w, 4); p += 4;   /* argv NULL */
    w = 0;        uc_mem_write(g_uc, p, &w, 4); p += 4;   /* envp NULL */
    for (int i = 0; i < naux; i++) {
        uc_mem_write(g_uc, p, &aux[i][0], 4); p += 4;
        uc_mem_write(g_uc, p, &aux[i][1], 4); p += 4;
    }
    return block;
}
