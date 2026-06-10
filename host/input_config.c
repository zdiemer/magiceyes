/* input_config -- see input_config.h. Hand-rolled config (no JSON/INI lib in the repo;
   mirrors the recent.txt/games.txt plain-text idiom in viewer.c). */
#include "input_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#define IC_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define IC_MKDIR(p) mkdir((p), 0755)
#endif

/* ---- bindable buttons + names (UI order; excludes the 4 computed diagonals) ---- */
static const struct { int bit; const char *name; } ic_btnnames[] = {
    { GP2X_UP, "UP" }, { GP2X_DOWN, "DOWN" }, { GP2X_LEFT, "LEFT" }, { GP2X_RIGHT, "RIGHT" },
    { GP2X_A, "A" }, { GP2X_B, "B" }, { GP2X_X, "X" }, { GP2X_Y, "Y" },
    { GP2X_L, "L" }, { GP2X_R, "R" }, { GP2X_START, "START" }, { GP2X_SELECT, "SELECT" },
    { GP2X_VOLUP, "VOLUP" }, { GP2X_VOLDOWN, "VOLDOWN" }, { GP2X_CLICK, "CLICK" },
};
#define IC_NBTN ((int)(sizeof ic_btnnames / sizeof ic_btnnames[0]))

const int ic_bindable[] = {
    GP2X_UP, GP2X_DOWN, GP2X_LEFT, GP2X_RIGHT,
    GP2X_A, GP2X_B, GP2X_X, GP2X_Y,
    GP2X_L, GP2X_R, GP2X_START, GP2X_SELECT,
    GP2X_VOLUP, GP2X_VOLDOWN, GP2X_CLICK,
};
const int ic_nbindable = (int)(sizeof ic_bindable / sizeof ic_bindable[0]);

const char *ic_button_name(int bit) {
    for (int i = 0; i < IC_NBTN; i++) if (ic_btnnames[i].bit == bit) return ic_btnnames[i].name;
    return "";
}
static int ic_name_to_bit(const char *name) {
    for (int i = 0; i < IC_NBTN; i++) if (!SDL_strcasecmp(ic_btnnames[i].name, name)) return ic_btnnames[i].bit;
    return -1;
}

/* ---- defaults ---- */
static void add_src(ic_binding *b, uint8_t type, int16_t code, int8_t dir) {
    if (b->nsrc >= IC_MAX_SRC) return;
    b->src[b->nsrc].type = type; b->src[b->nsrc].code = code; b->src[b->nsrc].dir = dir; b->nsrc++;
}
static void add_key(ic_binding *b, SDL_Scancode sc)               { add_src(b, IC_SRC_KEY,  (int16_t)sc, 0); }
static void add_cbtn(ic_binding *b, SDL_GameControllerButton bt)  { add_src(b, IC_SRC_CBTN, (int16_t)bt, 0); }
static void add_axis(ic_binding *b, SDL_GameControllerAxis ax, int8_t dir) { add_src(b, IC_SRC_AXIS, (int16_t)ax, dir); }

static void fill_profile(ic_profile *p) {
    memset(p, 0, sizeof *p);
    /* D-pad: keyboard arrows + gamepad dpad + left analog stick */
    add_key(&p->btn[GP2X_UP],    SDL_SCANCODE_UP);    add_cbtn(&p->btn[GP2X_UP],    SDL_CONTROLLER_BUTTON_DPAD_UP);    add_axis(&p->btn[GP2X_UP],    SDL_CONTROLLER_AXIS_LEFTY, -1);
    add_key(&p->btn[GP2X_DOWN],  SDL_SCANCODE_DOWN);  add_cbtn(&p->btn[GP2X_DOWN],  SDL_CONTROLLER_BUTTON_DPAD_DOWN);  add_axis(&p->btn[GP2X_DOWN],  SDL_CONTROLLER_AXIS_LEFTY, +1);
    add_key(&p->btn[GP2X_LEFT],  SDL_SCANCODE_LEFT);  add_cbtn(&p->btn[GP2X_LEFT],  SDL_CONTROLLER_BUTTON_DPAD_LEFT);  add_axis(&p->btn[GP2X_LEFT],  SDL_CONTROLLER_AXIS_LEFTX, -1);
    add_key(&p->btn[GP2X_RIGHT], SDL_SCANCODE_RIGHT); add_cbtn(&p->btn[GP2X_RIGHT], SDL_CONTROLLER_BUTTON_DPAD_RIGHT); add_axis(&p->btn[GP2X_RIGHT], SDL_CONTROLLER_AXIS_LEFTX, +1);
    /* face buttons: Z/X/A/S -> A/B/X/Y, gamepad A/B/X/Y */
    add_key(&p->btn[GP2X_A], SDL_SCANCODE_Z); add_cbtn(&p->btn[GP2X_A], SDL_CONTROLLER_BUTTON_A);
    add_key(&p->btn[GP2X_B], SDL_SCANCODE_X); add_cbtn(&p->btn[GP2X_B], SDL_CONTROLLER_BUTTON_B);
    add_key(&p->btn[GP2X_X], SDL_SCANCODE_A); add_cbtn(&p->btn[GP2X_X], SDL_CONTROLLER_BUTTON_X);
    add_key(&p->btn[GP2X_Y], SDL_SCANCODE_S); add_cbtn(&p->btn[GP2X_Y], SDL_CONTROLLER_BUTTON_Y);
    /* shoulders / start / select */
    add_key(&p->btn[GP2X_L], SDL_SCANCODE_Q); add_cbtn(&p->btn[GP2X_L], SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    add_key(&p->btn[GP2X_R], SDL_SCANCODE_W); add_cbtn(&p->btn[GP2X_R], SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    add_key(&p->btn[GP2X_START],  SDL_SCANCODE_RETURN); add_cbtn(&p->btn[GP2X_START], SDL_CONTROLLER_BUTTON_START);
    add_key(&p->btn[GP2X_SELECT], SDL_SCANCODE_BACKSPACE); add_key(&p->btn[GP2X_SELECT], SDL_SCANCODE_RSHIFT); add_cbtn(&p->btn[GP2X_SELECT], SDL_CONTROLLER_BUTTON_BACK);
    /* volume buttons (some games use these as real controls): '=' / '-' + gamepad triggers */
    add_key(&p->btn[GP2X_VOLUP],   SDL_SCANCODE_EQUALS); add_axis(&p->btn[GP2X_VOLUP],   SDL_CONTROLLER_AXIS_TRIGGERRIGHT, +1);
    add_key(&p->btn[GP2X_VOLDOWN], SDL_SCANCODE_MINUS);  add_axis(&p->btn[GP2X_VOLDOWN], SDL_CONTROLLER_AXIS_TRIGGERLEFT,  +1);
    /* stick click (Caanoo) */
    add_key(&p->btn[GP2X_CLICK], SDL_SCANCODE_SPACE); add_cbtn(&p->btn[GP2X_CLICK], SDL_CONTROLLER_BUTTON_LEFTSTICK);
}

void ic_load_defaults(ic_config *c) { for (int d = 0; d < IC_NDEV; d++) fill_profile(&c->prof[d]); }
void ic_reset_device(ic_config *c, int device) { if (device >= 0 && device < IC_NDEV) fill_profile(&c->prof[device]); }

/* ---- compute the canonical button bitmap ---- */
static int src_active(const ic_source *s, const Uint8 *kbd, SDL_GameController **pads, int npads) {
    switch (s->type) {
    case IC_SRC_KEY:
        return s->code >= 0 && s->code < SDL_NUM_SCANCODES && kbd && kbd[s->code];
    case IC_SRC_CBTN:
        for (int i = 0; i < npads; i++)
            if (pads[i] && SDL_GameControllerGetButton(pads[i], (SDL_GameControllerButton)s->code)) return 1;
        return 0;
    case IC_SRC_AXIS:
        for (int i = 0; i < npads; i++) {
            if (!pads[i]) continue;
            int v = SDL_GameControllerGetAxis(pads[i], (SDL_GameControllerAxis)s->code);
            if (s->dir >= 0 ? (v > IC_AXIS_THRESH) : (v < -IC_AXIS_THRESH)) return 1;
        }
        return 0;
    }
    return 0;
}

uint32_t ic_compute_buttons(const ic_config *c, const Uint8 *kbd,
                            SDL_GameController **pads, int npads, int device) {
    if (device < 0 || device >= IC_NDEV) device = 0;
    const ic_profile *p = &c->prof[device];
    uint32_t b = 0;
    for (int i = 0; i < ic_nbindable; i++) {
        int bit = ic_bindable[i];
        const ic_binding *bd = &p->btn[bit];
        for (int s = 0; s < bd->nsrc; s++)
            if (src_active(&bd->src[s], kbd, pads, npads)) { b |= 1u << bit; break; }
    }
    /* derive the 8-way diagonals from the cardinals (same rule as the old viewer block) */
    int up = !!(b & (1u << GP2X_UP)), dn = !!(b & (1u << GP2X_DOWN));
    int lf = !!(b & (1u << GP2X_LEFT)), rt = !!(b & (1u << GP2X_RIGHT));
    if (up && lf) b |= 1u << GP2X_UPLEFT;
    if (up && rt) b |= 1u << GP2X_UPRIGHT;
    if (dn && lf) b |= 1u << GP2X_DOWNLEFT;
    if (dn && rt) b |= 1u << GP2X_DOWNRIGHT;
    return b;
}

/* ---- source <-> token (on disk) and source -> human description (UI) ---- */
/* On-disk tokens: "key:<scancode_int>" (robust -- SDL scancode names contain spaces),
   "cbtn:<sdlname>", "axis:<sdlname><+|->". On read, key:<name> is also accepted (hand-edit). */
static int src_to_token(const ic_source *s, char *out, size_t cap) {
    switch (s->type) {
    case IC_SRC_KEY:  return snprintf(out, cap, "key:%d", (int)s->code);
    case IC_SRC_CBTN: {
        const char *n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)s->code);
        return snprintf(out, cap, "cbtn:%s", n ? n : "?");
    }
    case IC_SRC_AXIS: {
        const char *n = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)s->code);
        return snprintf(out, cap, "axis:%s%c", n ? n : "?", s->dir >= 0 ? '+' : '-');
    }
    }
    return 0;
}
static int token_to_src(const char *tok, ic_source *out) {
    memset(out, 0, sizeof *out);
    if (!strncmp(tok, "key:", 4)) {
        const char *v = tok + 4;
        if (isdigit((unsigned char)v[0])) out->code = (int16_t)atoi(v);
        else { SDL_Scancode sc = SDL_GetScancodeFromName(v); if (sc == SDL_SCANCODE_UNKNOWN) return 0; out->code = (int16_t)sc; }
        out->type = IC_SRC_KEY; return 1;
    }
    if (!strncmp(tok, "cbtn:", 5)) {
        SDL_GameControllerButton bt = SDL_GameControllerGetButtonFromString(tok + 5);
        if (bt == SDL_CONTROLLER_BUTTON_INVALID) return 0;
        out->type = IC_SRC_CBTN; out->code = (int16_t)bt; return 1;
    }
    if (!strncmp(tok, "axis:", 5)) {
        const char *v = tok + 5; size_t n = strlen(v);
        if (n < 2) return 0;
        char sign = v[n - 1]; if (sign != '+' && sign != '-') return 0;
        char name[64]; if (n - 1 >= sizeof name) return 0;
        memcpy(name, v, n - 1); name[n - 1] = 0;
        SDL_GameControllerAxis ax = SDL_GameControllerGetAxisFromString(name);
        if (ax == SDL_CONTROLLER_AXIS_INVALID) return 0;
        out->type = IC_SRC_AXIS; out->code = (int16_t)ax; out->dir = (sign == '+') ? +1 : -1; return 1;
    }
    return 0;
}

/* Title-case an SDL axis name for display: "lefty" -> "LeftY", "triggerright" -> "TriggerRight". */
static void axis_pretty(const char *n, char *out, size_t cap) {
    size_t o = 0; int up = 1;
    for (size_t i = 0; n[i] && o + 1 < cap; i++) {
        char ch = n[i];
        if (ch == 'l' && up && !strncmp(n + i, "left", 4))  { out[o++] = 'L'; up = 0; continue; }
        if (ch == 'r' && up && !strncmp(n + i, "right", 5)) { out[o++] = 'R'; up = 0; continue; }
        if (ch == 't' && up && !strncmp(n + i, "trigger", 7)) { out[o++] = 'T'; up = 0; continue; }
        if (ch == 'x' || ch == 'y') { out[o++] = (char)toupper((unsigned char)ch); continue; }
        if (!strncmp(n + i, "right", 5) && o > 0) { out[o++] = 'R'; i += 4; continue; }
        if (!strncmp(n + i, "left", 4)  && o > 0) { out[o++] = 'L'; i += 3; continue; }
        out[o++] = up ? (char)toupper((unsigned char)ch) : ch; up = 0;
    }
    out[o] = 0;
}

void ic_source_describe(const ic_source *s, char *out, size_t cap) {
    switch (s->type) {
    case IC_SRC_KEY: {
        const char *n = SDL_GetScancodeName((SDL_Scancode)s->code);
        snprintf(out, cap, "%s", (n && *n) ? n : "Key?");
        break;
    }
    case IC_SRC_CBTN: {
        const char *n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)s->code);
        char pretty[32]; pretty[0] = 0;
        if (n) { snprintf(pretty, sizeof pretty, "%c%s", toupper((unsigned char)n[0]), n + 1); }
        snprintf(out, cap, "Pad:%s", pretty[0] ? pretty : "?");
        break;
    }
    case IC_SRC_AXIS: {
        const char *n = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)s->code);
        char pretty[40]; pretty[0] = 0; if (n) axis_pretty(n, pretty, sizeof pretty);
        snprintf(out, cap, "Pad:%s%c", pretty[0] ? pretty : "?", s->dir >= 0 ? '+' : '-');
        break;
    }
    default: snprintf(out, cap, "?"); break;
    }
}

void ic_binding_describe(const ic_binding *b, char *out, size_t cap) {
    if (b->nsrc == 0) { snprintf(out, cap, "(unbound)"); return; }
    size_t o = 0; out[0] = 0;
    for (int i = 0; i < b->nsrc && o + 1 < cap; i++) {
        char one[64]; ic_source_describe(&b->src[i], one, sizeof one);
        o += snprintf(out + o, cap - o, "%s%s", i ? ", " : "", one);
    }
}

/* ---- config file path + load/save ---- */
static char s_config_dir[512];   /* override set by the bundle (me_paths Settings dir); "" = default */

void ic_set_config_dir(const char *dir) {
    if (dir && dir[0]) snprintf(s_config_dir, sizeof s_config_dir, "%s", dir);
    else               s_config_dir[0] = 0;
}

static int ic_config_path(char *out, size_t cap) {
    if (s_config_dir[0]) {                        /* portable override (bundle: me_paths Settings) */
        IC_MKDIR(s_config_dir);
        snprintf(out, cap, "%s/bindings.conf", s_config_dir);
        return 1;
    }
#ifdef _WIN32
    const char *base = getenv("APPDATA"); if (!base) base = getenv("TEMP"); if (!base) base = ".";
    char dir[512]; snprintf(dir, sizeof dir, "%s\\magiceyes", base); IC_MKDIR(dir);
    snprintf(out, cap, "%s\\bindings.conf", dir);
#else
    const char *base = getenv("HOME"); if (!base) base = ".";
    char dir[512]; snprintf(dir, sizeof dir, "%s/.magiceyes", base); IC_MKDIR(dir);
    snprintf(out, cap, "%s/bindings.conf", dir);
#endif
    return 1;
}

static const char *dev_section[IC_NDEV] = { "gp2x", "wiz", "caanoo" };

int ic_save(const ic_config *c) {
    char path[1024]; ic_config_path(path, sizeof path);
    FILE *f = fopen(path, "w"); if (!f) return -1;
    fprintf(f, "# magiceyes-bindings v1\n");
    for (int d = 0; d < IC_NDEV; d++) {
        fprintf(f, "\n[%s]\n", dev_section[d]);
        for (int i = 0; i < ic_nbindable; i++) {
            int bit = ic_bindable[i];
            const ic_binding *bd = &c->prof[d].btn[bit];
            fprintf(f, "%-8s=", ic_button_name(bit));
            for (int s = 0; s < bd->nsrc; s++) {
                char tok[64]; src_to_token(&bd->src[s], tok, sizeof tok);
                fprintf(f, " %s", tok);
            }
            fputc('\n', f);
        }
    }
    fclose(f);
    return 0;
}

int ic_load(ic_config *c) {
    ic_load_defaults(c);
    char path[1024]; ic_config_path(path, sizeof path);
    FILE *f = fopen(path, "r"); if (!f) return 0;
    int dev = -1; char line[512];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        if (*p == '[') {                                  /* [section] -> select device profile */
            char name[64]; if (sscanf(p, "[%63[^]]]", name) == 1) {
                dev = -1;
                for (int d = 0; d < IC_NDEV; d++) if (!SDL_strcasecmp(name, dev_section[d])) dev = d;
            }
            continue;
        }
        if (dev < 0) continue;                            /* line outside any known section */
        char *eq = strchr(p, '='); if (!eq) continue;
        *eq = 0;
        char *name = p; char *e = name + strlen(name);    /* trim button name */
        while (e > name && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        int bit = ic_name_to_bit(name); if (bit < 0) continue;
        ic_binding *bd = &c->prof[dev].btn[bit];
        bd->nsrc = 0;                                     /* a line in the file overrides the default */
        char *tok = strtok(eq + 1, " \t\r\n");
        while (tok && bd->nsrc < IC_MAX_SRC) {
            ic_source s; if (token_to_src(tok, &s)) bd->src[bd->nsrc++] = s;
            tok = strtok(NULL, " \t\r\n");
        }
    }
    fclose(f);
    return 1;
}
