/* magiceyes -- portable, user-configurable storage paths. See paths.h.
 *
 * Defaults put Settings/Firmware/Cache in dirs beside the executable so the bundle is fully
 * portable. Overrides live in <exe_dir>/paths.conf (key=value: settings=, firmware=, cache=);
 * the anchor sits at the exe dir -- NOT in the Settings dir -- so relocating Settings can't
 * hide its own config. */
#include "engine.h"
#include "paths.h"
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define ME_MKDIR(p) _mkdir(p)
#else
#define ME_MKDIR(p) mkdir(p, 0777)
#endif

extern char g_exe_dir[PATH_MAX];   /* absolutised in main.c; may be empty very early */

static char s_override[ME_PATH_NKINDS][PATH_MAX];   /* user override; "" = portable default */
static int  s_loaded = 0;

static const char *kind_key(me_path_kind k) {
    switch (k) {
    case ME_PATH_SETTINGS: return "settings";
    case ME_PATH_FIRMWARE: return "firmware";
    case ME_PATH_CACHE:    return "cache";
    default:               return "";
    }
}
const char *me_paths_label(me_path_kind k) {
    switch (k) {
    case ME_PATH_SETTINGS: return "Settings";
    case ME_PATH_FIRMWARE: return "Firmware";
    case ME_PATH_CACHE:    return "Cache";
    default:               return "?";
    }
}

/* Absolute dir of our own executable. Prefer g_exe_dir (absolutised in main); fall back to the
   OS so resolution never depends on init ordering or an odd argv[0]; "." as a last resort. */
static void exe_dir(char *out, size_t cap) {
    if (g_exe_dir[0]) { snprintf(out, cap, "%s", g_exe_dir); return; }
#ifdef _WIN32
    wchar_t w[PATH_MAX]; DWORD n = GetModuleFileNameW(NULL, w, PATH_MAX);
    if (n > 0 && n < PATH_MAX) {
        char p[PATH_MAX]; WideCharToMultiByte(CP_UTF8, 0, w, -1, p, sizeof p, NULL, NULL);
        char *s1 = strrchr(p, '\\'), *s2 = strrchr(p, '/'), *s = s1 > s2 ? s1 : s2;
        if (s) *s = 0;
        snprintf(out, cap, "%s", p[0] ? p : "."); return;
    }
#else
    char p[PATH_MAX]; ssize_t n = readlink("/proc/self/exe", p, sizeof p - 1);
    if (n > 0) { p[n] = 0; char *s = strrchr(p, '/'); if (s) *s = 0; snprintf(out, cap, "%s", p[0] ? p : "."); return; }
#endif
    snprintf(out, cap, ".");
}

static void anchor_path(char *out, size_t cap) {
    char dir[PATH_MAX]; exe_dir(dir, sizeof dir);
    snprintf(out, cap, "%s/paths.conf", dir);
}

/* mkdir -p of `dir` (every component, then the leaf). */
static void mkdirs(const char *dir) {
    char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s", dir);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/' || *p == '\\') { char c = *p; *p = 0; ME_MKDIR(tmp); *p = c; }
    ME_MKDIR(tmp);
}

static void rstrip(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\r' || s[n-1] == '\n' || s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
}

static void load_once(void) {
    if (s_loaded) return;
    s_loaded = 1;
    char p[PATH_MAX]; anchor_path(p, sizeof p);
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[PATH_MAX + 64];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = 0; char *key = line, *val = eq + 1; rstrip(key); rstrip(val);
        for (int k = 0; k < ME_PATH_NKINDS; k++)
            if (!strcmp(key, kind_key(k))) { snprintf(s_override[k], PATH_MAX, "%s", val); break; }
    }
    fclose(f);
}

static void save(void) {
    char p[PATH_MAX]; anchor_path(p, sizeof p);
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "# magiceyes storage paths -- absolute dirs; a missing key means the portable\n");
    fprintf(f, "# default (a dir beside the exe). Edit via File > Settings... in-app.\n");
    for (int k = 0; k < ME_PATH_NKINDS; k++)
        if (s_override[k][0]) fprintf(f, "%s=%s\n", kind_key(k), s_override[k]);
    fclose(f);
}

void me_paths_default(me_path_kind k, char *out, size_t cap) {
    char dir[PATH_MAX]; exe_dir(dir, sizeof dir);
    const char *sub = (k == ME_PATH_SETTINGS) ? "config"
                    : (k == ME_PATH_FIRMWARE) ? "firmware"
                    : "cache";
    snprintf(out, cap, "%s/%s", dir, sub);
}

void me_paths_dir(me_path_kind k, char *out, size_t cap) {
    load_once();
    if (k < 0 || k >= ME_PATH_NKINDS) { snprintf(out, cap, "."); return; }
    if (s_override[k][0]) snprintf(out, cap, "%s", s_override[k]);
    else                  me_paths_default(k, out, cap);
    mkdirs(out);
}

int me_paths_set(me_path_kind k, const char *dir) {
    load_once();
    if (k < 0 || k >= ME_PATH_NKINDS) return -1;
    if (!dir || !dir[0]) s_override[k][0] = 0;             /* clear -> portable default */
    else { snprintf(s_override[k], PATH_MAX, "%s", dir); mkdirs(s_override[k]); }
    save();
    return 0;
}

void me_paths_reset(void) {
    load_once();
    for (int k = 0; k < ME_PATH_NKINDS; k++) s_override[k][0] = 0;
    save();
}
