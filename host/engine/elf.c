/* magiceyes Unicorn engine — ELF loader (static ET_EXEC) + SysV stack setup.
 * (A dynamic-linker / PT_INTERP path for Wiz titles lands here later.) */
#include "engine.h"

/* ---- ELF loader (static EXEC) ---- */
uint32_t load_elf(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("open elf"); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(sz);
    if (fread(buf, 1, sz, f) != (size_t)sz) { perror("read elf"); exit(1); }
    fclose(f);

    Elf32_Ehdr *eh = (Elf32_Ehdr *)buf;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32)
        { fprintf(stderr, "not a 32-bit ELF\n"); exit(1); }
    if (eh->e_machine != EM_ARM) { fprintf(stderr, "not ARM\n"); exit(1); }
    if (eh->e_type != ET_EXEC) { fprintf(stderr, "only static ET_EXEC for now\n"); exit(1); }

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

    /* build the initial stack block; align so final sp is 8-aligned */
    uint32_t aux[][2] = {
        {6 /*AT_PAGESZ*/, PAGE},
        {25/*AT_RANDOM*/, at_random},
        {0 /*AT_NULL*/, 0},
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
