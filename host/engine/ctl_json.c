/* Minimal JSON emit/parse for the control channel. See ctl_json.h. */
#include "engine.h"
#include "ctl_json.h"

/* ---- writer --------------------------------------------------------------- */
static void jw_reserve(struct jw *w, size_t extra) {
    if (w->err) return;
    if (w->len + extra + 1 <= w->cap) return;
    size_t cap = w->cap ? w->cap : 512;
    while (cap < w->len + extra + 1) cap *= 2;
    char *nb = realloc(w->buf, cap);
    if (!nb) { w->err = 1; return; }
    w->buf = nb; w->cap = cap;
}

void jw_init(struct jw *w) { memset(w, 0, sizeof *w); }
void jw_free(struct jw *w) { free(w->buf); memset(w, 0, sizeof *w); }

void jw_raw(struct jw *w, const char *s) {
    size_t n = strlen(s);
    jw_reserve(w, n);
    if (w->err) return;
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = 0;
}

void jw_str(struct jw *w, const char *s) {
    if (!s) { jw_raw(w, "null"); return; }
    jw_reserve(w, strlen(s) * 6 + 2);
    if (w->err) return;
    char *o = w->buf + w->len;
    *o++ = '"';
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  *o++ = '\\'; *o++ = '"';  break;
        case '\\': *o++ = '\\'; *o++ = '\\'; break;
        case '\n': *o++ = '\\'; *o++ = 'n';  break;
        case '\r': *o++ = '\\'; *o++ = 'r';  break;
        case '\t': *o++ = '\\'; *o++ = 't';  break;
        default:
            if (*p < 0x20) { o += sprintf(o, "\\u%04x", *p); }
            else *o++ = (char)*p;
        }
    }
    *o++ = '"';
    w->len = (size_t)(o - w->buf);
    w->buf[w->len] = 0;
}

void jw_comma(struct jw *w) {
    if (w->len && w->buf[w->len - 1] != '{' && w->buf[w->len - 1] != '[') jw_raw(w, ",");
}

void jw_key(struct jw *w, const char *k) { jw_comma(w); jw_str(w, k); jw_raw(w, ":"); }

void jw_kv_str(struct jw *w, const char *k, const char *v) { jw_key(w, k); jw_str(w, v); }

void jw_kv_i64(struct jw *w, const char *k, long long v) {
    char b[32]; snprintf(b, sizeof b, "%lld", v);
    jw_key(w, k); jw_raw(w, b);
}

void jw_kv_u32(struct jw *w, const char *k, uint32_t v) { jw_kv_i64(w, k, (long long)v); }

void jw_kv_dbl(struct jw *w, const char *k, double v) {
    char b[40];
    if (v != v || v > 1e308 || v < -1e308) snprintf(b, sizeof b, "null");  /* JSON has no NaN/Inf */
    else snprintf(b, sizeof b, "%.17g", v);
    jw_key(w, k); jw_raw(w, b);
}

void jw_kv_bool(struct jw *w, const char *k, int v) { jw_key(w, k); jw_raw(w, v ? "true" : "false"); }

/* ---- parser --------------------------------------------------------------- */
static const char *skip_ws(const char *p, const char *e) {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/* Copy a JSON string body (p just past the opening quote) into dst, honouring the escapes we
   emit. Returns the position just past the closing quote, or NULL if unterminated. */
static const char *take_string(const char *p, const char *e, char *dst, size_t cap) {
    size_t o = 0;
    while (p < e && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < e) {
            char x = *p++;
            switch (x) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'u':
                p += (e - p >= 4) ? 4 : (e - p);   /* not needed by the protocol: drop the codepoint */
                continue;
            default:  c = x;    break;
            }
        }
        if (o + 1 < cap) dst[o++] = c;
    }
    if (p >= e) return NULL;
    dst[o] = 0;
    return p + 1;
}

int jp_parse(struct jp *jp, const char *s, size_t len) {
    const char *p = s, *e = s + len;
    memset(jp, 0, sizeof *jp);
    p = skip_ws(p, e);
    if (p >= e || *p != '{') return -1;
    p++;
    for (;;) {
        p = skip_ws(p, e);
        if (p < e && *p == '}') return 0;
        if (p >= e || *p != '"') return -1;
        p++;
        if (jp->n >= JP_MAXKEYS) return -1;
        p = take_string(p, e, jp->key[jp->n], sizeof jp->key[0]);
        if (!p) return -1;
        p = skip_ws(p, e);
        if (p >= e || *p != ':') return -1;
        p++;
        p = skip_ws(p, e);
        if (p >= e) return -1;
        if (*p == '"') {
            p++;
            p = take_string(p, e, jp->val[jp->n], sizeof jp->val[0]);
            if (!p) return -1;
        } else {
            /* number / true / false / null -- copy the token verbatim */
            size_t o = 0;
            while (p < e && *p != ',' && *p != '}' && *p != ' ' && *p != '\n' && *p != '\r'
                   && *p != '\t') {
                if (o + 1 < sizeof jp->val[0]) jp->val[jp->n][o++] = *p;
                p++;
            }
            jp->val[jp->n][o] = 0;
        }
        jp->n++;
        p = skip_ws(p, e);
        if (p < e && *p == ',') { p++; continue; }
        if (p < e && *p == '}') return 0;
        return -1;
    }
}

const char *jp_get(const struct jp *p, const char *key) {
    for (int i = 0; i < p->n; i++) if (!strcmp(p->key[i], key)) return p->val[i];
    return NULL;
}

long long jp_int(const struct jp *p, const char *key, long long def) {
    const char *v = jp_get(p, key);
    if (!v || !*v) return def;
    char *end = NULL;
    long long r = strtoll(v, &end, 0);   /* base 0: accepts 0x-hex, which addresses come as */
    return (end && end != v) ? r : def;
}
