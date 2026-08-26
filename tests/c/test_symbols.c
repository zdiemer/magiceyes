/* Unit tests for host/engine/symbols.c -- ELF symtab indexing and nearest-preceding lookup.
 *
 * sym_load_image_buf takes a BUFFER, so the fixtures here are ELF images built in memory: no
 * files, no game assets, no toolchain. That also lets each test shape exactly the symbol table it
 * wants (a NOTYPE assembly label, a mapping symbol, a sizeless .dynsym entry) instead of hoping a
 * real binary happens to contain one.
 *
 * symbols.c makes zero uc_* calls but includes engine.h, so it needs the unicorn headers and one
 * stubbed global: g_trace. The Elf32_* types come from the elf.h that symbols.c itself includes
 * (the MinGW build gets host/win/compat/elf.h).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "symbols.c"

/* host/win/compat/elf.h is a minimal shim carrying only what symbols.c itself needs, so a few
   standard constants used by the fixture builder below are absent on the MinGW build. Their
   values are fixed by the ELF specification. Defined here rather than added to the shim so the
   shipping Windows build is untouched by a test's needs. */
#ifndef EI_DATA
#define EI_DATA     5
#endif
#ifndef ELFDATA2LSB
#define ELFDATA2LSB 1
#endif
#ifndef EV_CURRENT
#define EV_CURRENT  1
#endif
#ifndef STB_GLOBAL
#define STB_GLOBAL  1
#endif
#ifndef ELF32_ST_INFO
#define ELF32_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0xf))
#endif

int g_trace = 0;      /* the one engine global symbols.c refers to */

/* ---- an ELF image builder -------------------------------------------------------------------- */

struct symspec {
    const char   *name;
    uint32_t      value, size;
    unsigned char type;      /* STT_FUNC / STT_OBJECT / STT_NOTYPE / ... */
    uint16_t      shndx;     /* 1 = a normal defined section */
};

struct elfbuf { uint8_t b[16384]; long n; };

#define SEC_NULL 0
#define SEC_SYM  1
#define SEC_STR  2
#define SEC_SHSTR 3
#define SEC_N    4

/* Build a minimal but valid ELF32 carrying one symbol table. `symtab` picks SHT_SYMTAB vs
   SHT_DYNSYM so the .symtab-preferred / .dynsym-fallback logic can be exercised. */
static void build_elf(struct elfbuf *e, const struct symspec *syms, int nsyms, int symtab) {
    memset(e, 0, sizeof *e);

    static const char SHSTR[] = "\0.symtab\0.dynsym\0.strtab\0.shstrtab";
    const uint32_t shstr_off = 64;
    const uint32_t shstr_len = sizeof SHSTR;
    memcpy(e->b + shstr_off, SHSTR, shstr_len);

    /* .strtab: a leading NUL then each name */
    uint32_t str_off = shstr_off + shstr_len;
    uint32_t str_len = 1;
    uint32_t name_off[32];
    for (int i = 0; i < nsyms; i++) {
        name_off[i] = str_len;
        size_t n = strlen(syms[i].name) + 1;
        memcpy(e->b + str_off + str_len, syms[i].name, n);
        str_len += (uint32_t)n;
    }

    /* symbol table: index 0 is the reserved null entry */
    uint32_t sym_off = (str_off + str_len + 3) & ~3u;
    Elf32_Sym *sy = (Elf32_Sym *)(e->b + sym_off);
    memset(&sy[0], 0, sizeof sy[0]);
    for (int i = 0; i < nsyms; i++) {
        sy[i + 1].st_name  = name_off[i];
        sy[i + 1].st_value = syms[i].value;
        sy[i + 1].st_size  = syms[i].size;
        sy[i + 1].st_info  = (unsigned char)ELF32_ST_INFO(STB_GLOBAL, syms[i].type);
        sy[i + 1].st_other = 0;
        sy[i + 1].st_shndx = syms[i].shndx;
    }
    uint32_t sym_len = (uint32_t)((nsyms + 1) * (int)sizeof(Elf32_Sym));

    uint32_t sh_off = (sym_off + sym_len + 3) & ~3u;
    Elf32_Shdr *sh = (Elf32_Shdr *)(e->b + sh_off);
    memset(sh, 0, SEC_N * sizeof *sh);

    sh[SEC_SYM].sh_name    = symtab ? 1 : 9;          /* ".symtab" or ".dynsym" */
    sh[SEC_SYM].sh_type    = symtab ? SHT_SYMTAB : SHT_DYNSYM;
    sh[SEC_SYM].sh_offset  = sym_off;
    sh[SEC_SYM].sh_size    = sym_len;
    sh[SEC_SYM].sh_link    = SEC_STR;
    sh[SEC_SYM].sh_entsize = sizeof(Elf32_Sym);

    sh[SEC_STR].sh_name   = 17;                        /* ".strtab" */
    sh[SEC_STR].sh_type   = SHT_STRTAB;
    sh[SEC_STR].sh_offset = str_off;
    sh[SEC_STR].sh_size   = str_len;

    sh[SEC_SHSTR].sh_name   = 25;                      /* ".shstrtab" */
    sh[SEC_SHSTR].sh_type   = SHT_STRTAB;
    sh[SEC_SHSTR].sh_offset = shstr_off;
    sh[SEC_SHSTR].sh_size   = shstr_len;

    Elf32_Ehdr *eh = (Elf32_Ehdr *)e->b;
    memcpy(eh->e_ident, ELFMAG, SELFMAG);
    eh->e_ident[EI_CLASS] = ELFCLASS32;
    eh->e_ident[EI_DATA]  = ELFDATA2LSB;
    eh->e_type      = ET_EXEC;
    eh->e_machine   = EM_ARM;
    eh->e_version   = EV_CURRENT;
    eh->e_ehsize    = sizeof(Elf32_Ehdr);
    eh->e_shoff     = sh_off;
    eh->e_shentsize = sizeof(Elf32_Shdr);
    eh->e_shnum     = SEC_N;
    eh->e_shstrndx  = SEC_SHSTR;

    e->n = (long)(sh_off + SEC_N * sizeof(Elf32_Shdr));
}

static int setup(void **st) { (void)st; sym_reset(); return 0; }

/* ---- loading ---------------------------------------------------------------------------------- */

static void test_loads_a_symtab(void **st) {
    (void)st;
    const struct symspec syms[] = {
        {"main",       0x8000, 0x40, STT_FUNC,   1},
        {"helper",     0x8040, 0x20, STT_FUNC,   1},
        {"some_table", 0x9000, 0x10, STT_OBJECT, 1},
    };
    struct elfbuf e;
    build_elf(&e, syms, 3, 1);

    assert_int_equal(sym_load_image_buf(e.b, e.n, 0, "game.gpe"), 3);
    assert_int_equal(sym_count(), 3);
}

static void test_find_by_name(void **st) {
    (void)st;
    const struct symspec syms[] = {
        {"main",   0x8000, 0x40, STT_FUNC, 1},
        {"helper", 0x8040, 0x20, STT_FUNC, 1},
    };
    struct elfbuf e;
    build_elf(&e, syms, 2, 1);
    sym_load_image_buf(e.b, e.n, 0, "game.gpe");

    uint32_t addr = 0, size = 0;
    assert_true(sym_find("helper", &addr, &size));
    assert_true(addr == 0x8040);
    assert_true(size == 0x20);

    assert_false(sym_find("nonexistent", &addr, &size));
    assert_false(sym_find("", &addr, &size));
    assert_false(sym_find(NULL, &addr, &size));
}

/* A `.global label` in hand-written assembly is STT_NOTYPE. Filtering to FUNC/OBJECT would
   silently lose exactly the labels the control-channel selftest looks up in smoke_spin.S. */
static void test_notype_assembly_labels_are_kept(void **st) {
    (void)st;
    const struct symspec syms[] = {
        {"_start",    0x8000, 0,    STT_NOTYPE, 1},
        {"step_zone", 0x8004, 0,    STT_NOTYPE, 1},
        {"real_func", 0x8100, 0x10, STT_FUNC,   1},
    };
    struct elfbuf e;
    build_elf(&e, syms, 3, 1);
    sym_load_image_buf(e.b, e.n, 0, "spin");

    uint32_t addr = 0;
    assert_true(sym_find("step_zone", &addr, NULL));
    assert_true(addr == 0x8004);
}

/* ARM mapping symbols share addresses with real functions and would dominate any listing. */
static void test_mapping_symbols_are_skipped(void **st) {
    (void)st;
    const struct symspec syms[] = {
        {"$a",    0x8000, 0,    STT_NOTYPE, 1},
        {"$d",    0x8004, 0,    STT_NOTYPE, 1},
        {"$t.1",  0x8008, 0,    STT_NOTYPE, 1},
        {"main",  0x8010, 0x20, STT_FUNC,   1},
        {"$real", 0x8040, 0x10, STT_FUNC,   1},   /* not a mapping symbol: 3+ chars, no dot */
    };
    struct elfbuf e;
    build_elf(&e, syms, 5, 1);
    sym_load_image_buf(e.b, e.n, 0, "game");

    assert_false(sym_find("$a", NULL, NULL));
    assert_false(sym_find("$d", NULL, NULL));
    assert_false(sym_find("$t.1", NULL, NULL));
    assert_true(sym_find("main", NULL, NULL));
    assert_true(sym_find("$real", NULL, NULL));
}

/* Undefined, absolute and zero-valued symbols carry no usable address. */
static void test_undefined_absolute_and_zero_are_skipped(void **st) {
    (void)st;
    const struct symspec syms[] = {
        {"imported", 0x1000, 0x10, STT_FUNC, SHN_UNDEF},
        {"absolute", 0x2000, 0x10, STT_OBJECT, SHN_ABS},
        {"zero",     0,      0x10, STT_FUNC, 1},
        {"real",     0x8000, 0x10, STT_FUNC, 1},
    };
    struct elfbuf e;
    build_elf(&e, syms, 4, 1);

    assert_int_equal(sym_load_image_buf(e.b, e.n, 0, "game"), 1);
    assert_true(sym_find("real", NULL, NULL));
    assert_false(sym_find("imported", NULL, NULL));
    assert_false(sym_find("absolute", NULL, NULL));
    assert_false(sym_find("zero", NULL, NULL));
}

/* Types other than FUNC/OBJECT/NOTYPE are not addresses worth naming. */
static void test_other_symbol_types_are_skipped(void **st) {
    (void)st;
    const struct symspec syms[] = {
        {"a_file.c", 0x8000, 0, STT_FILE,    1},
        {"a_sec",    0x8100, 0, STT_SECTION, 1},
        {"real",     0x8200, 0x10, STT_FUNC, 1},
    };
    struct elfbuf e;
    build_elf(&e, syms, 3, 1);
    assert_int_equal(sym_load_image_buf(e.b, e.n, 0, "game"), 1);
}

/* The bias is what makes a shared library's symbols land at their mapped addresses. */
static void test_bias_is_applied(void **st) {
    (void)st;
    const struct symspec syms[] = {{"libfn", 0x1000, 0x10, STT_FUNC, 1}};
    struct elfbuf e;
    build_elf(&e, syms, 1, 1);
    sym_load_image_buf(e.b, e.n, 0x40000000u, "libfoo.so");

    uint32_t addr = 0;
    assert_true(sym_find("libfn", &addr, NULL));
    assert_true(addr == 0x40001000u);
}

/* .symtab wins outright: a binary carrying both must not have its .dynsym folded in as well. */
static void test_dynsym_is_only_the_fallback(void **st) {
    (void)st;
    const struct symspec syms[] = {{"dynfn", 0x8000, 0x10, STT_FUNC, 1}};
    struct elfbuf e;
    build_elf(&e, syms, 1, 0);        /* SHT_DYNSYM only */
    assert_int_equal(sym_load_image_buf(e.b, e.n, 0, "stripped"), 1);
    assert_true(sym_find("dynfn", NULL, NULL));
}

static void test_rejects_non_elf_input(void **st) {
    (void)st;
    uint8_t junk[512];
    memset(junk, 0x41, sizeof junk);
    assert_int_equal(sym_load_image_buf(junk, sizeof junk, 0, "junk"), 0);
    assert_int_equal(sym_load_image_buf(NULL, 512, 0, "null"), 0);
    assert_int_equal(sym_load_image_buf(junk, 4, 0, "tiny"), 0);
    assert_int_equal(sym_count(), 0);
}

/* A section-header table that runs off the end of the buffer must be refused, not walked. */
static void test_rejects_a_truncated_section_header_table(void **st) {
    (void)st;
    const struct symspec syms[] = {{"main", 0x8000, 0x10, STT_FUNC, 1}};
    struct elfbuf e;
    build_elf(&e, syms, 1, 1);
    assert_int_equal(sym_load_image_buf(e.b, e.n - 8, 0, "cut"), 0);
    assert_int_equal(sym_count(), 0);
}

/* A stripped binary has no section headers at all, which is the common GP2X case. */
static void test_rejects_an_image_with_no_sections(void **st) {
    (void)st;
    const struct symspec syms[] = {{"main", 0x8000, 0x10, STT_FUNC, 1}};
    struct elfbuf e;
    build_elf(&e, syms, 1, 1);
    ((Elf32_Ehdr *)e.b)->e_shnum = 0;
    assert_int_equal(sym_load_image_buf(e.b, e.n, 0, "stripped"), 0);
}

/* ---- lookup ------------------------------------------------------------------------------------ */

static void load_lookup_fixture(void) {
    const struct symspec syms[] = {
        {"first",  0x8000, 0x40, STT_FUNC, 1},
        {"second", 0x8100, 0x20, STT_FUNC, 1},
        {"third",  0x8200, 0x10, STT_FUNC, 1},
    };
    struct elfbuf e;
    build_elf(&e, syms, 3, 1);
    sym_load_image_buf(e.b, e.n, 0, "/some/dir/game.gpe");
}

static void test_lookup_exact_and_interior(void **st) {
    (void)st;
    load_lookup_fixture();

    const char *name = NULL, *image = NULL;
    uint32_t off = 0xffff;
    assert_true(sym_lookup(0x8100, &name, &off, &image));
    assert_string_equal(name, "second");
    assert_int_equal(off, 0);

    assert_true(sym_lookup(0x8110, &name, &off, NULL));
    assert_string_equal(name, "second");
    assert_int_equal(off, 0x10);
}

/* The size gate is the point: without it a preceding symbol confidently "explains" an address
   that is nowhere near it, which reads as fact and sends you to the wrong function. */
static void test_lookup_declines_outside_a_sized_symbol(void **st) {
    (void)st;
    load_lookup_fixture();

    const char *name = NULL;
    assert_true(sym_lookup(0x813f, &name, NULL, NULL) == 0);   /* past second's 0x20 size */
    assert_false(sym_lookup(0x8210, NULL, NULL, NULL));        /* past third */
    assert_false(sym_lookup(0x7fff, NULL, NULL, NULL));        /* below everything */
}

/* Sizeless symbols (common in .dynsym) get a bounded slack window instead of an open-ended one. */
static void test_lookup_slack_window_for_sizeless_symbols(void **st) {
    (void)st;
    const struct symspec syms[] = {{"sizeless", 0x8000, 0, STT_NOTYPE, 1}};
    struct elfbuf e;
    build_elf(&e, syms, 1, 1);
    sym_load_image_buf(e.b, e.n, 0, "game");

    const char *name = NULL;
    uint32_t off = 0;
    assert_true(sym_lookup(0x8000 + 0x1000, &name, &off, NULL));
    assert_string_equal(name, "sizeless");
    assert_int_equal(off, 0x1000);
    assert_false(sym_lookup(0x8000 + 0x1001, NULL, NULL, NULL));
}

static void test_lookup_on_an_empty_index(void **st) {
    (void)st;
    assert_false(sym_lookup(0x8000, NULL, NULL, NULL));
}

/* The image is reported by basename, which is what a backtrace line has room for. */
static void test_lookup_reports_the_image_basename(void **st) {
    (void)st;
    load_lookup_fixture();
    const char *image = NULL;
    assert_true(sym_lookup(0x8000, NULL, NULL, &image));
    assert_string_equal(image, "game.gpe");
}

/* ---- iteration and reset ------------------------------------------------------------------------ */

/* sym_iter walks the sorted index, so a listing comes out in address order regardless of the
   order the symbol table happened to store them in. */
static void test_iter_is_address_ordered(void **st) {
    (void)st;
    const struct symspec syms[] = {
        {"c", 0x9000, 0x10, STT_FUNC, 1},
        {"a", 0x8000, 0x10, STT_FUNC, 1},
        {"b", 0x8800, 0x10, STT_FUNC, 1},
    };
    struct elfbuf e;
    build_elf(&e, syms, 3, 1);
    sym_load_image_buf(e.b, e.n, 0, "game");

    uint32_t prev = 0;
    const char *name = NULL;
    for (int i = 0; i < sym_count(); i++) {
        uint32_t addr = 0;
        assert_true(sym_iter(i, &addr, &name, NULL));
        assert_true(addr >= prev);
        prev = addr;
    }
    assert_false(sym_iter(-1, NULL, NULL, NULL));
    assert_false(sym_iter(sym_count(), NULL, NULL, NULL));
}

static void test_reset_clears_everything(void **st) {
    (void)st;
    load_lookup_fixture();
    assert_true(sym_count() > 0);
    sym_reset();
    assert_int_equal(sym_count(), 0);
    assert_false(sym_find("first", NULL, NULL));
    assert_false(sym_lookup(0x8000, NULL, NULL, NULL));
}

/* A reload indexes the next title from scratch; symbols must not survive across it, and the
   arena being reused must not corrupt the new names. */
static void test_reload_reindexes_cleanly(void **st) {
    (void)st;
    load_lookup_fixture();
    sym_reset();

    const struct symspec syms[] = {{"other_title_fn", 0xa000, 0x10, STT_FUNC, 1}};
    struct elfbuf e;
    build_elf(&e, syms, 1, 1);
    sym_load_image_buf(e.b, e.n, 0, "other.gpe");

    uint32_t addr = 0;
    assert_true(sym_find("other_title_fn", &addr, NULL));
    assert_true(addr == 0xa000);
    assert_false(sym_find("first", NULL, NULL));
    assert_int_equal(sym_count(), 1);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_loads_a_symtab, setup),
        cmocka_unit_test_setup(test_find_by_name, setup),
        cmocka_unit_test_setup(test_notype_assembly_labels_are_kept, setup),
        cmocka_unit_test_setup(test_mapping_symbols_are_skipped, setup),
        cmocka_unit_test_setup(test_undefined_absolute_and_zero_are_skipped, setup),
        cmocka_unit_test_setup(test_other_symbol_types_are_skipped, setup),
        cmocka_unit_test_setup(test_bias_is_applied, setup),
        cmocka_unit_test_setup(test_dynsym_is_only_the_fallback, setup),
        cmocka_unit_test_setup(test_rejects_non_elf_input, setup),
        cmocka_unit_test_setup(test_rejects_a_truncated_section_header_table, setup),
        cmocka_unit_test_setup(test_rejects_an_image_with_no_sections, setup),
        cmocka_unit_test_setup(test_lookup_exact_and_interior, setup),
        cmocka_unit_test_setup(test_lookup_declines_outside_a_sized_symbol, setup),
        cmocka_unit_test_setup(test_lookup_slack_window_for_sizeless_symbols, setup),
        cmocka_unit_test_setup(test_lookup_on_an_empty_index, setup),
        cmocka_unit_test_setup(test_lookup_reports_the_image_basename, setup),
        cmocka_unit_test_setup(test_iter_is_address_ordered, setup),
        cmocka_unit_test_setup(test_reset_clears_everything, setup),
        cmocka_unit_test_setup(test_reload_reindexes_cleanly, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
