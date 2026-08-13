/* magiceyes — structured run telemetry (see report.h). */

#include "engine.h"     /* DIAG, PATH_MAX, pthread */
#include "report.h"

#define MR_MAX 512      /* distinct events; repeats just bump a count, so this is plenty */

struct mr_entry {
    int      kind;
    long     code;
    char     name[80];
    uint32_t count;
    uint32_t pc;        /* first-seen guest PC */
};

static struct mr_entry g_ev[MR_MAX];
static int             g_nev;
static pthread_mutex_t g_mr_lock = PTHREAD_MUTEX_INITIALIZER;
static int             g_mr_active;
static char            g_mr_path[PATH_MAX];

static const char *KIND_STR[MR_KIND_COUNT] = {
    "host_fault", "guest_fatal", "missing_rootfs_lib", "missing_symbol", "unimpl_syscall",
    "unknown_dev", "unknown_ioctl", "unknown_mmio", "unsupported_blit", "unsupported_gles",
    "unsupported_audio", "unsupported_sdl",
};

const char *me_report_kind_str(int kind) {
    return (kind >= 0 && kind < MR_KIND_COUNT) ? KIND_STR[kind] : "unknown";
}

int  me_report_active(void) { return g_mr_active; }

void me_report_init(const char *path) {
    g_mr_active = 1;
    if (path && *path) snprintf(g_mr_path, sizeof g_mr_path, "%s", path);
}

void me_report_reset(void) {
    pthread_mutex_lock(&g_mr_lock);
    g_nev = 0;
    memset(g_ev, 0, sizeof g_ev);
    pthread_mutex_unlock(&g_mr_lock);
}

void me_report(int kind, long code, const char *name, uint32_t pc) {
    if (!g_mr_active) return;        /* off by default: no capture, no log noise, no cost */
    if (kind < 0 || kind >= MR_KIND_COUNT) return;
    int isnew = 0;
    pthread_mutex_lock(&g_mr_lock);
    for (int i = 0; i < g_nev; i++) {                  /* dedup by (kind, code, name) */
        if (g_ev[i].kind == kind && g_ev[i].code == code &&
            ((!name && !g_ev[i].name[0]) || (name && !strcmp(g_ev[i].name, name)))) {
            g_ev[i].count++;
            pthread_mutex_unlock(&g_mr_lock);
            return;
        }
    }
    if (g_nev < MR_MAX) {
        struct mr_entry *e = &g_ev[g_nev++];
        e->kind = kind; e->code = code; e->count = 1; e->pc = pc;
        if (name) snprintf(e->name, sizeof e->name, "%s", name); else e->name[0] = 0;
        isnew = 1;
    }
    pthread_mutex_unlock(&g_mr_lock);
    if (isnew) {                                       /* mirror to the human log, once per event */
        if (name && name[0])
            fprintf(DIAG, "  [report] %s code=%ld (%s) pc=%08x\n",
                    me_report_kind_str(kind), code, name, pc);
        else
            fprintf(DIAG, "  [report] %s code=%ld pc=%08x\n",
                    me_report_kind_str(kind), code, pc);
    }
}

/* Copy a single trimmed line out of `s` into out[cap] (stop at newline; collapse leading ws). */
static void first_line(const char *s, char *out, size_t cap) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    size_t j = 0;
    for (; *s && *s != '\n' && *s != '\r' && j < cap - 1; s++) out[j++] = *s;
    out[j] = 0;
}

#define MR_SENTINEL "\x01MR "   /* guest-shim report line prefix (see report.h) */
int me_report_ingest_guest(const char *buf, size_t len) {
    if (len < 4 || memcmp(buf, MR_SENTINEL, 4) != 0) return 0;   /* not a shim report line */
    if (!g_mr_active) return 1;                                  /* swallow it; we're not capturing */
    char tmp[256];
    size_t n = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
    memcpy(tmp, buf, n); tmp[n] = 0;
    char *p = tmp + 4;
    long kind = strtol(p, &p, 10);
    long code = strtol(p, &p, 10);
    while (*p == ' ') p++;
    char name[80]; first_line(p, name, sizeof name);
    me_report((int)kind, code, name[0] ? name : NULL, 0);
    return 1;
}

void me_report_scan_write(int guest_fd, const char *buf, size_t len) {
    if (!g_mr_active || (guest_fd != 1 && guest_fd != 2) || !buf || !len) return;
    char tmp[1024];
    size_t n = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
    memcpy(tmp, buf, n); tmp[n] = 0;
    char line[80];
    if (strstr(tmp, "symbol lookup error") || strstr(tmp, "undefined symbol")) {
        first_line(tmp, line, sizeof line);
        me_report(MR_MISSING_SYMBOL, 0, line, 0);
    } else if (strstr(tmp, "error while loading shared libraries") ||
               strstr(tmp, "cannot open shared object")) {
        first_line(tmp, line, sizeof line);
        me_report(MR_MISSING_ROOTFS_LIB, 0, line, 0);
    } else if ((strstr(tmp, "assertion") && strstr(tmp, "failed")) ||
               /* glibc fortify/heap aborts -- match the specific banners, NOT a bare "*** ":
                  games love decorating benign stderr ("*** INIT SOUND ***", penguin-command)
                  and one false guest_fatal demotes a fine title to `incompatible`. */
               strstr(tmp, "*** glibc detected") ||
               strstr(tmp, "*** stack smashing detected") ||
               strstr(tmp, "*** buffer overflow detected") ||
               strstr(tmp, "*** longjmp causes uninitialized stack")) {
        first_line(tmp, line, sizeof line);
        me_report(MR_GUEST_FATAL, 0, line, 0);
    }
}

static void json_escape(FILE *f, const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c == '\n') fputs("\\n", f);
        else if (c == '\t') fputs("\\t", f);
        else if (c < 0x20)  fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
}

/* Build the report JSON into a malloc'd buffer. Shared by the file writer and the control channel
   (which needs the same document in-process, and cannot use open_memstream -- MinGW has none). */
void me_report_json_buf(char **out, size_t *outlen) {
    *out = NULL;
    if (outlen) *outlen = 0;
    /* Bound: the table is fixed at 512 entries, each well under 256 bytes rendered. */
    size_t cap = 512 + (size_t)g_nev * 300 + MR_KIND_COUNT * 48;
    char *b = malloc(cap);
    if (!b) return;
    size_t o = 0;
    long counts[MR_KIND_COUNT] = {0};

    pthread_mutex_lock(&g_mr_lock);
    o += (size_t)snprintf(b + o, cap - o, "{\"events\":[");
    for (int i = 0; i < g_nev; i++) {
        struct mr_entry *e = &g_ev[i];
        counts[e->kind] += e->count;
        o += (size_t)snprintf(b + o, cap - o, "%s{\"kind\":\"%s\",\"code\":%ld,\"name\":\"",
                              i ? "," : "", me_report_kind_str(e->kind), e->code);
        for (const char *s = e->name; *s && o + 8 < cap; s++) {
            unsigned char c = (unsigned char)*s;
            if (c == '"' || c == '\\')  o += (size_t)snprintf(b + o, cap - o, "\\%c", c);
            else if (c == '\n')         o += (size_t)snprintf(b + o, cap - o, "\\n");
            else if (c == '\t')         o += (size_t)snprintf(b + o, cap - o, "\\t");
            else if (c < 0x20)          o += (size_t)snprintf(b + o, cap - o, "\\u%04x", c);
            else                        b[o++] = (char)c;
        }
        o += (size_t)snprintf(b + o, cap - o, "\",\"count\":%u,\"pc\":\"0x%08x\"}", e->count, e->pc);
    }
    o += (size_t)snprintf(b + o, cap - o, "],\"counts\":{");
    int first = 1;
    for (int k = 0; k < MR_KIND_COUNT; k++) if (counts[k]) {
        o += (size_t)snprintf(b + o, cap - o, "%s\"%s\":%ld",
                              first ? "" : ",", me_report_kind_str(k), counts[k]);
        first = 0;
    }
    o += (size_t)snprintf(b + o, cap - o, "}}");
    pthread_mutex_unlock(&g_mr_lock);

    *out = b;
    if (outlen) *outlen = o;
}

void me_report_flush_json(const char *path) {
    if (!path || !*path) path = g_mr_path;
    if (!path || !*path) return;
    char *buf = NULL; size_t len = 0;
    me_report_json_buf(&buf, &len);
    if (!buf) return;
    FILE *f = fopen(path, "w");
    if (f) { fwrite(buf, 1, len, f); fputc('\n', f); fclose(f); }
    free(buf);
}
