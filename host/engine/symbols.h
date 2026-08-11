/* ELF symbol lookup for the debugger (symbols.c). Additive: many titles are stripped statics with
   no .symtab at all, so th_backtrace()'s bl/blx-validated stack scan stays the fallback. */
#ifndef MAGICEYES_SYMBOLS_H
#define MAGICEYES_SYMBOLS_H

#include <stdint.h>

/* Index an ELF already in memory (the loader has the buffer before it frees it). `bias` is added
   to every st_value: 0 for the fixed-address main binary, the load base for the interpreter.
   Returns how many symbols were added. */
int sym_load_image_buf(const uint8_t *buf, long sz, uint32_t bias, const char *label);
int sym_load_image(const char *host_path, uint32_t bias, const char *label);

/* A shared object was just mapped executable at `map_base` by the guest's ld.so (called from the
   mmap2 syscall, the only point where a library's load base is observable). Derives the load bias
   from the file's own PT_LOADs and indexes it. Idempotent per path. */
void sym_note_lib(const char *host_path, uint32_t map_base);

/* addr -> nearest preceding symbol. Requires the address to fall inside the symbol's size when it
   has one, so a lookup declines rather than confidently naming the wrong function. */
int sym_lookup(uint32_t addr, const char **name, uint32_t *off, const char **image);
int sym_find(const char *name, uint32_t *addr, uint32_t *size);
int sym_count(void);
int sym_iter(int i, uint32_t *addr, const char **name, const char **image);
void sym_reset(void);   /* per reload */

#endif /* MAGICEYES_SYMBOLS_H */
