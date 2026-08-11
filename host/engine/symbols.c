/* ELF symbol table -> address lookup, so a backtrace or a breakpoint can be talked about by name.
 *
 * Generalises the section-header walk that oabi_libm.c already does for .dynsym/.rel.plt, to
 * .symtab/.strtab as well, and keeps a sorted index for nearest-preceding lookup.
 *
 * Be realistic about the yield: GP2X .gpe are GPEComp self-extractors that decompress to a
 * STATICALLY linked, usually stripped binary, so many titles have no .symtab at all. That is why
 * this is additive -- the bl/blx-validated stack scan in th_backtrace() remains the fallback and
 * is frequently the only backtrace available. Where symbols DO exist (unstripped homebrew, the
 * interpreter, firmware libraries) they turn a page of hex into something readable. */
#include "engine.h"
#include "symbols.h"

#ifdef _WIN32
#include "../win/compat/elf.h"
#else
#include <elf.h>
#endif

#define SYM_MAX     20000
#define IMG_MAX     24
#define ARENA_CHUNK (256 * 1024)

struct sym { uint32_t addr, size; uint32_t name_off; int img; };

static struct sym  g_sym[SYM_MAX];
static int         g_nsym = 0;
static int        *g_order = NULL;      /* indices into g_sym, sorted by addr */
static char       *g_arena = NULL;
static size_t      g_arena_len = 0, g_arena_cap = 0;
static char        g_img[IMG_MAX][64];
static int         g_nimg = 0;
static int         g_sorted = 0;

static uint32_t arena_put(const char *s) {
    size_t n = strlen(s) + 1;
    if (g_arena_len + n > g_arena_cap) {
        size_t cap = g_arena_cap ? g_arena_cap * 2 : ARENA_CHUNK;
        while (cap < g_arena_len + n) cap *= 2;
        char *nb = realloc(g_arena, cap);
        if (!nb) return 0;
        g_arena = nb; g_arena_cap = cap;
    }
    uint32_t off = (uint32_t)g_arena_len;
    memcpy(g_arena + g_arena_len, s, n);
    g_arena_len += n;
    return off;
}

static int img_put(const char *label) {
    if (g_nimg >= IMG_MAX) return g_nimg - 1;
    const char *b = strrchr(label, '/');
    b = b ? b + 1 : label;
    snprintf(g_img[g_nimg], sizeof g_img[0], "%s", b);
    return g_nimg++;
}

static int cmp_addr(const void *a, const void *b) {
    uint32_t x = g_sym[*(const int *)a].addr, y = g_sym[*(const int *)b].addr;
    return (x > y) - (x < y);
}

static void sort_index(void) {
    if (g_sorted || !g_nsym) return;
    free(g_order);
    g_order = malloc((size_t)g_nsym * sizeof(int));
    if (!g_order) return;
    for (int i = 0; i < g_nsym; i++) g_order[i] = i;
    qsort(g_order, (size_t)g_nsym, sizeof(int), cmp_addr);
    g_sorted = 1;
}

int sym_load_image_buf(const uint8_t *buf, long sz, uint32_t bias, const char *label) {
    if (!buf || sz < (long)sizeof(Elf32_Ehdr)) return 0;
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)buf;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) return 0;
    if (!eh->e_shoff || !eh->e_shnum || eh->e_shstrndx >= eh->e_shnum) return 0;
    if (eh->e_shoff + (long)eh->e_shnum * eh->e_shentsize > sz) return 0;

    const Elf32_Shdr *sh = (const Elf32_Shdr *)(buf + eh->e_shoff);
    if (sh[eh->e_shstrndx].sh_offset >= (uint32_t)sz) return 0;
    const char *shstr = (const char *)(buf + sh[eh->e_shstrndx].sh_offset);

    int img = img_put(label ? label : "?");
    int added = 0;

    /* Prefer .symtab (full, includes local/static functions); .dynsym is the stripped fallback. */
    for (int pass = 0; pass < 2; pass++) {
        uint32_t want = pass == 0 ? SHT_SYMTAB : SHT_DYNSYM;
        for (int i = 0; i < eh->e_shnum; i++) {
            if (sh[i].sh_type != want || !sh[i].sh_entsize) continue;
            if (sh[i].sh_link >= eh->e_shnum) continue;
            if (sh[i].sh_offset + sh[i].sh_size > (uint32_t)sz) continue;
            const Elf32_Shdr *strsec = &sh[sh[i].sh_link];
            if (strsec->sh_offset >= (uint32_t)sz) continue;
            const char *str = (const char *)(buf + strsec->sh_offset);
            const Elf32_Sym *sy = (const Elf32_Sym *)(buf + sh[i].sh_offset);
            uint32_t n = sh[i].sh_size / sh[i].sh_entsize;
            for (uint32_t k = 0; k < n && g_nsym < SYM_MAX; k++) {
                unsigned char t = ELF32_ST_TYPE(sy[k].st_info);
                /* NOTYPE is accepted deliberately: a plain `.global label` in hand-written
                   assembly, and plenty of real text symbols, carry no type (these are exactly the
                   "T" entries nm prints). Filtering to FUNC/OBJECT silently loses them. Junk is
                   held back instead by requiring a defined section and a non-zero value, by the
                   mapping-symbol filter below, and by the size gate in sym_lookup. */
                if (t != STT_FUNC && t != STT_OBJECT && t != STT_NOTYPE) continue;
                if (!sy[k].st_value || sy[k].st_shndx == SHN_UNDEF ||
                    sy[k].st_shndx == SHN_ABS) continue;
                const char *nm = str + sy[k].st_name;
                if (!nm || !*nm) continue;
                /* Skip ARM mapping symbols ($a/$d/$t mark ARM/data/Thumb regions). They share
                   addresses with real functions and would otherwise dominate any listing. */
                if (nm[0] == '$' && nm[1] && (nm[2] == 0 || nm[2] == '.')) continue;
                uint32_t off = arena_put(nm);
                if (!off && g_arena_len) continue;
                g_sym[g_nsym].addr     = bias + sy[k].st_value;
                g_sym[g_nsym].size     = sy[k].st_size;
                g_sym[g_nsym].name_off = off;
                g_sym[g_nsym].img      = img;
                g_nsym++; added++;
            }
        }
        if (added) break;      /* .symtab was present -- do not also fold in .dynsym */
    }
    if (added) { g_sorted = 0; sort_index(); }
    (void)shstr;
    return added;
}

int sym_load_image(const char *host_path, uint32_t bias, const char *label) {
    if (!host_path || !*host_path) return 0;
    FILE *f = fopen(host_path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64 * 1024 * 1024) { fclose(f); return 0; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return 0; }
    int got = (fread(buf, 1, (size_t)sz, f) == (size_t)sz);
    fclose(f);
    int n = got ? sym_load_image_buf(buf, sz, bias, label ? label : host_path) : 0;
    free(buf);
    return n;
}

int sym_lookup(uint32_t addr, const char **name, uint32_t *off, const char **image) {
    sort_index();
    if (!g_nsym || !g_order) return 0;
    /* nearest symbol at or below addr */
    int lo = 0, hi = g_nsym - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_sym[g_order[mid]].addr <= addr) { best = g_order[mid]; lo = mid + 1; }
        else hi = mid - 1;
    }
    if (best < 0) return 0;
    uint32_t delta = addr - g_sym[best].addr;
    /* If the symbol has a size, require the address to fall inside it. Sizeless symbols (common in
       .dynsym) only get a small slack window -- with a large one, a section-start symbol like
       _init happily "explains" an address 9KB away, which reads as fact and sends you to the wrong
       function. Declining is more useful than a confident wrong answer. */
    if (g_sym[best].size) { if (delta >= g_sym[best].size) return 0; }
    else if (delta > 0x1000) return 0;
    if (name)  *name  = g_arena + g_sym[best].name_off;
    if (off)   *off   = delta;
    if (image) *image = g_img[g_sym[best].img];
    return 1;
}

int sym_find(const char *name, uint32_t *addr, uint32_t *size) {
    if (!name || !*name) return 0;
    for (int i = 0; i < g_nsym; i++)
        if (!strcmp(g_arena + g_sym[i].name_off, name)) {
            if (addr) *addr = g_sym[i].addr;
            if (size) *size = g_sym[i].size;
            return 1;
        }
    return 0;
}

int sym_count(void) { return g_nsym; }

int sym_iter(int i, uint32_t *addr, const char **name, const char **image) {
    if (i < 0 || i >= g_nsym) return 0;
    sort_index();
    int k = g_order ? g_order[i] : i;
    if (addr)  *addr  = g_sym[k].addr;
    if (name)  *name  = g_arena + g_sym[k].name_off;
    if (image) *image = g_img[g_sym[k].img];
    return 1;
}

void sym_reset(void) {
    g_nsym = 0; g_nimg = 0; g_arena_len = 0; g_sorted = 0;
    free(g_order); g_order = NULL;
    /* keep the arena allocation for the next title */
}
