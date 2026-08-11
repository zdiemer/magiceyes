/* Minimal JSON emit/parse for the control channel (ctl.c).
 *
 * Deliberately tiny and dependency-free: the engine links no JSON library, and the control
 * protocol only ever needs a flat object (scalar and string values) in each direction. Anything
 * that would need nesting on the REQUEST side is expressed as separate keys instead. */
#ifndef MAGICEYES_CTL_JSON_H
#define MAGICEYES_CTL_JSON_H

#include <stddef.h>
#include <stdint.h>

/* ---- writer: a growable buffer -------------------------------------------- */
struct jw { char *buf; size_t len, cap; int err; };

void jw_init(struct jw *w);
void jw_free(struct jw *w);
void jw_raw(struct jw *w, const char *s);          /* append verbatim */
void jw_str(struct jw *w, const char *s);          /* append a JSON-escaped quoted string */
void jw_key(struct jw *w, const char *k);          /* "k": */
void jw_kv_str(struct jw *w, const char *k, const char *v);
void jw_kv_i64(struct jw *w, const char *k, long long v);
void jw_kv_u32(struct jw *w, const char *k, uint32_t v);
void jw_kv_dbl(struct jw *w, const char *k, double v);
void jw_kv_bool(struct jw *w, const char *k, int v);
void jw_comma(struct jw *w);                        /* ',' if the last char isn't '{' or '[' */

/* ---- parser: flat object -------------------------------------------------- */
#define JP_MAXKEYS 24
struct jp {
    int n;
    char key[JP_MAXKEYS][24];
    char val[JP_MAXKEYS][192];   /* strings unquoted; numbers/bools kept as text */
};

/* Parse a flat JSON object. Returns 0 on success, -1 on malformed input. Values longer than the
   slot are truncated (the protocol has no long request values). */
int  jp_parse(struct jp *p, const char *s, size_t len);
const char *jp_get(const struct jp *p, const char *key);
/* Numeric accessor accepting decimal or 0x-hex; `def` when the key is absent/unparsable. */
long long jp_int(const struct jp *p, const char *key, long long def);

#endif /* MAGICEYES_CTL_JSON_H */
