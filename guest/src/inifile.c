/* GPH "inifile" API (GPH_SDK/include/inifile.h) — the firmware's libinifile.so, reimplemented.
 *
 * Caanoo titles use it for settings/highscores two ways: some NEED libinifile.so directly
 * (Demons), others resolve INI_* from the firmware's fat libSDL exports. So this file is built
 * BOTH into the fake-SDL shim (exported alongside SDL_*) and as a standalone libinifile.so
 * (stage_rootfs_eabi.sh). Writes go through fopen(), so the engine's save overlay captures them.
 *
 * Semantics chosen to be permissive: INI_Open on a missing file succeeds with an empty table
 * (reads yield the caller's defaults, writes create the file on INI_Close). One global file at a
 * time, like the firmware's (the header has no handle type). Comments in the original file are
 * not preserved across a rewrite. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ini_ent { char *group, *key, *val; struct ini_ent *next; };

static struct ini_ent *g_ents;
static char g_path[1024];
static int  g_open, g_dirty;

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1; char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\r'||e[-1]=='\n')) *--e = 0;
    return s;
}
static void ini_free(void) {
    while (g_ents) { struct ini_ent *n = g_ents->next;
        free(g_ents->group); free(g_ents->key); free(g_ents->val); free(g_ents);
        g_ents = n; }
    g_open = g_dirty = 0; g_path[0] = 0;
}
static struct ini_ent *ini_find(const char *group, const char *key) {
    for (struct ini_ent *e = g_ents; e; e = e->next)
        if (!strcasecmp(e->group, group ? group : "") && !strcasecmp(e->key, key)) return e;
    return NULL;
}
static void ini_parse_line(char *line, char *group, size_t gcap) {
    char *s = trim(line);
    if (!*s || *s == ';' || *s == '#') return;
    if (*s == '[') { char *e = strchr(s, ']'); if (e) { *e = 0; snprintf(group, gcap, "%s", s + 1); } return; }
    char *eq = strchr(s, '=');
    if (!eq) return;
    *eq = 0;
    char *k = trim(s), *v = trim(eq + 1);
    struct ini_ent *ent = malloc(sizeof *ent);
    if (!ent) return;
    ent->group = xstrdup(group); ent->key = xstrdup(k); ent->val = xstrdup(v);
    /* append (keep file order for the rewrite) */
    ent->next = NULL;
    if (!g_ents) g_ents = ent;
    else { struct ini_ent *t = g_ents; while (t->next) t = t->next; t->next = ent; }
}

int INI_Open(const char *file) {
    ini_free();
    if (!file) return 0;
    snprintf(g_path, sizeof g_path, "%s", file);
    g_open = 1;
    FILE *f = fopen(file, "rb");
    if (f) {
        char line[1024], group[256] = "";
        while (fgets(line, sizeof line, f)) ini_parse_line(line, group, sizeof group);
        fclose(f);
    }
    return 1;
}
int INI_Open_Mem(const char *src, int size) {
    ini_free();
    if (!src || size <= 0) return 0;
    g_open = 1;                       /* no path: read-only table, INI_Close discards writes */
    char *buf = malloc((size_t)size + 1);
    if (!buf) return 0;
    memcpy(buf, src, (size_t)size); buf[size] = 0;
    char group[256] = "";
    for (char *p = buf; p && *p; ) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        ini_parse_line(p, group, sizeof group);
        p = nl ? nl + 1 : NULL;
    }
    free(buf);
    return 1;
}
void INI_Close(void) {
    if (g_dirty && g_path[0]) {
        FILE *f = fopen(g_path, "wb");
        if (f) {
            const char *cur = NULL;
            for (struct ini_ent *e = g_ents; e; e = e->next) {
                if (!cur || strcasecmp(cur, e->group)) {
                    if (e->group[0]) fprintf(f, "%s[%s]\n", cur ? "\n" : "", e->group);
                    cur = e->group;
                }
                fprintf(f, "%s=%s\n", e->key, e->val);
            }
            fclose(f);
        }
    }
    ini_free();
}

const char *INI_ReadText(const char *group, const char *entry, const char *defval) {
    struct ini_ent *e = entry ? ini_find(group, entry) : NULL;
    return e ? e->val : defval;
}
int INI_ReadInt(const char *group, const char *entry, const int defval) {
    const char *v = INI_ReadText(group, entry, NULL);
    return v ? (int)strtol(v, NULL, 0) : defval;
}
int INI_ReadBool(const char *group, const char *entry, const int defval) {
    const char *v = INI_ReadText(group, entry, NULL);
    if (!v) return defval;
    if (!strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on")) return 1;
    if (!strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off")) return 0;
    return strtol(v, NULL, 0) != 0;
}
float INI_ReadFloat(const char *group, const char *entry, const float defval) {
    const char *v = INI_ReadText(group, entry, NULL);
    return v ? (float)strtod(v, NULL) : defval;
}

void INI_WriteText(const char *group, const char *entry, const char *val) {
    if (!g_open || !entry || !val) return;
    struct ini_ent *e = ini_find(group, entry);
    if (e) { free(e->val); e->val = xstrdup(val); }
    else {
        char line[1024]; snprintf(line, sizeof line, "%s=%s", entry, val);
        char grp[256]; snprintf(grp, sizeof grp, "%s", group ? group : "");
        ini_parse_line(line, grp, sizeof grp);
    }
    g_dirty = 1;
}
void INI_WriteInt(const char *group, const char *entry, const int val) {
    char b[32]; snprintf(b, sizeof b, "%d", val); INI_WriteText(group, entry, b);
}
void INI_WriteBool(const char *group, const char *entry, const int val) {
    INI_WriteText(group, entry, val ? "true" : "false");
}
void INI_WriteFloat(const char *group, const char *entry, const float val) {
    char b[48]; snprintf(b, sizeof b, "%g", (double)val); INI_WriteText(group, entry, b);
}
