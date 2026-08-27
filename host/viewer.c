/* Native x86 SDL2 viewer for the GP2X shim.
 * Maps /dev/shm/gp2x_fb, shows the RGB565 framebuffer in a scaled window
 * (X11/Wayland), feeds keyboard input back as GP2X buttons, and plays the PCM
 * ring the shim produces. Build with host gcc + SDL2 (NOT the ARM toolchain).
 */
#ifdef ME_BUNDLED
#define SDL_MAIN_HANDLED 1   /* bundled: the engine owns main(); don't link SDL2main */
#endif
#include <SDL2/SDL.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "gp2xshm.h"
#include "input_config.h"
#include "settings_ui.h"
#include <time.h>       /* the savestate toast reports how old a state is */

/* Savestates. state_file.h is the CONTAINER only (framing, slot paths, the thumbnail probe) and
   pulls in nothing but stdint, so it is safe on both builds: the standalone viewer's slot picker
   reads a state's thumbnail through it without linking the engine at all. The me_state_* slot
   wrappers below are engine entry points and are only called in the single-process bundle, where
   the viewer runs as an engine thread; elsewhere a request goes through the shm bytes. */
#include "state_file.h"
#ifdef ME_BUNDLED
int  me_state_save_slot(int slot, char *err, size_t ecap);
int  me_state_load_slot(int slot, char *err, size_t ecap);
int  me_state_slot_path_for_current(int slot, char *out, size_t cap);
#endif

#if defined(_WIN32) && defined(ME_BUNDLED)
/* Native Win32 menu bar (File/View/Audio/Help) on the SDL window. The window proc + SDL's
   event pump run on this (the viewer) thread, so the menu, WM_COMMAND, and the file dialog
   all live here -- no thread hop. File->Open hot-reloads via the engine (same process). */
#include <SDL2/SDL_syswm.h>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>   /* SHBrowseForFolder (games-folder picker) */
#include <direct.h>   /* _mkdir for the recent-files dir */
/* engine entry points (plain C signatures -- avoid including engine.h / unicorn here) */
const char *resolve_input(const char *in, char *out, size_t cap);
int  classify_elf(const char *path);
int  read_elf_interp(const char *path, char *out, size_t cap);     /* dynamic title's PT_INTERP */
int  me_rootfs_select(const char *interp);                         /* pick the matching device rootfs */
void engine_request_reload(const char *host_path);
int  me_firmware_install(const char *file, const char *device);    /* stage a firmware .zip/.img */
int  me_firmware_boot_request(const char *device);                 /* boot a staged firmware menu */
int  me_firmware_paths(const char *device, char *rootfs, char *menu, size_t cap);  /* installed? */
extern int g_fwlog;                        /* ME_FWLOG: debug logging to the engine log */
void me_log(const char *fmt, ...);
#define ME_WINMENU 1
#include "keybind_win.h"   /* native Win32 keybinding editor (replaces the SDL overlay here) */
#include "paths_win.h"     /* native Win32 storage-folder editor (Settings/Firmware/Cache) */
#include "state_win.h"     /* native Win32 savestate slot picker */
#include "paths.h"         /* me_paths_* -- portable storage roots (no engine deps) */
#endif

/* "an input editor is open" -- pause game input + suppress the SDL overlay's input while either
   the native Win32 keybind window (bundle) or the in-SDL settings overlay (elsewhere) is up. */
#ifdef ME_WINMENU
#define EDIT_OPEN() (kbwin_is_open() || paths_win_is_open() || state_win_is_open() || su_is_open(&g_su))
#else
#define EDIT_OPEN() (su_is_open(&g_su))
#endif

#ifdef ME_BUNDLED
/* Crash-recovery handoff from the engine (guard.c): when a game hits a host fault the engine
   tears it down and sets g_fault_pending; the viewer thread polls it and tells the user the
   emulator is still alive. Externs only (no engine.h here). g_cur_game is read as a string. */
extern volatile int g_fault_pending;
extern volatile uintptr_t g_fault_addr;
extern char g_cur_game[];
#else
/* Standalone viewer (no engine): the record/replay module below logs via the engine's me_log
   and names recordings after the engine's current-game string -- provide light fallbacks so the
   two-process viewer.exe/viewer still builds. */
#include <stdarg.h>
static char g_cur_game[1];
static void me_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
#endif

static gp2x_shm_t *shm;
/* gamepad + remappable input bindings + the keybind settings overlay */
#define ME_MAXPADS 4
SDL_GameController *g_pads[ME_MAXPADS]; int g_npads = 0;   /* (non-static: keybind_win.c polls these) */
static ic_config g_ic;
static su_state  g_su;
static unsigned long long g_consumed = 0;   /* real audio bytes played (B/s stat) */
static unsigned long long g_fed = 0;        /* all bytes queued incl. silence pad */
static volatile int g_view_mute = 0;        /* Audio->Mute / --mute */
static volatile int g_view_volume = 100;    /* Audio->Volume / --volume (0..100) */

/* Queue PCM applying mute/volume. At 100% and unmuted, queue straight from the ring (fast
   path). Otherwise scale into a scratch buffer: S16 samples scale linearly; U8 around 128. */
static FILE *g_adump = NULL;   /* ME_AUDIODUMP: raw capture of the exact stream queued to SDL */
static void queue_scaled(SDL_AudioDeviceID dev, const uint8_t *src, uint32_t n) {
    int vol = g_view_mute ? 0 : g_view_volume;
    if (g_adump) fwrite(src, 1, n, g_adump);
    if (vol >= 100) { SDL_QueueAudio(dev, src, n); return; }
    int s16 = ((shm->audio_format & 0xff) == 16);
    static uint8_t buf[16384];
    for (uint32_t off = 0; off < n; ) {
        uint32_t c = n - off; if (c > sizeof buf) c = sizeof buf;
        if (s16) {
            const int16_t *s = (const int16_t *)(src + off); int16_t *d = (int16_t *)buf;
            for (uint32_t i = 0; i < c / 2; i++) d[i] = (int16_t)((int)s[i] * vol / 100);
        } else {
            for (uint32_t i = 0; i < c; i++) buf[i] = (uint8_t)(128 + ((int)src[off + i] - 128) * vol / 100);
        }
        SDL_QueueAudio(dev, buf, c);
        off += c;
    }
}

/* Audio runs on its OWN thread: SDL_CloseAudioDevice/OpenAudioDevice can take up
   to ~1s on some audio backends, so reopening a wedged device from the render loop
   would freeze the whole window. Here a reopen only stalls audio, never rendering. */
static int audio_thread(void *arg) {
    (void)arg;
    SDL_AudioDeviceID adev = 0; int audio_open = 0;
    unsigned long long wd_played = 0; Uint32 wd_t = 0, stat_t = 0;
    if (getenv("ME_AUDIODUMP")) g_adump = fopen(getenv("ME_AUDIODUMP"), "wb");
    while (!shm->quit) {
        if (!audio_open && shm->audio_active && shm->audio_freq) {
            SDL_AudioSpec want, have;
            memset(&want, 0, sizeof(want));
            want.freq = (int)shm->audio_freq;
            want.format = (Uint16)shm->audio_format;
            want.channels = (Uint8)shm->audio_channels;
            want.samples = 4096;                  /* device buffer */
            want.callback = NULL;                 /* queue (push) mode */
            adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (adev) { audio_open = 1; SDL_PauseAudioDevice(adev, 0);
                fprintf(stderr, "viewer: audio %dHz fmt=%04x ch=%d (queue mode)\n",
                        want.freq, want.format, want.channels); }
            else { fprintf(stderr, "viewer: SDL_OpenAudioDevice failed: %s\n",
                           SDL_GetError()); SDL_Delay(100); }
        }
        if (audio_open) {
            uint32_t frame = shm->audio_channels * 2; if (frame < 2) frame = 4;
            uint32_t bps = shm->audio_freq * frame;
            uint32_t target = bps / 5;            /* keep ~200ms queued */
            uint32_t queued = SDL_GetQueuedAudioSize(adev);
            uint32_t avail = shm->a_write - shm->a_read;
            /* Top the device queue up to ~200ms with REAL audio. */
            if (queued < target) {
                uint32_t n = target - queued; if (n > avail) n = avail; n -= n % frame;
                if (n) {
                    uint32_t r = shm->a_read % GP2XSHM_ARING;
                    uint32_t first = GP2XSHM_ARING - r; if (first > n) first = n;
                    queue_scaled(adev, shm->aring + r, first);
                    if (n > first) queue_scaled(adev, shm->aring, n - first);
                    shm->a_read += n; g_consumed += n; g_fed += n;
                }
            }
            /* Silence-fill ONLY a genuine underrun (queue nearly dry) -- NOT every cycle. A
               small-chunk producer (Blazar/Quartz2 stream ~20ms / 3528-byte writes vs Payback's
               ~93ms / 16384) briefly leaves avail==0 between writes; padding to target on those
               gaps injects silence clicks AND advances the queue with silence so the real audio
               backs up in the ring and gets dropped -> static. The ~200ms real-audio queue rides
               out the brief gaps; only bridge a true underrun so the device never hard-stops. */
            { uint32_t q = SDL_GetQueuedAudioSize(adev), floor = bps / 50; /* ~20ms cushion */
              if (q < frame) {
                  uint32_t still = floor - q; still -= still % frame;
                  static uint8_t zeros[8192];
                  for (uint32_t z = still; z; ) { uint32_t c = z > sizeof(zeros) ? sizeof(zeros) : z;
                      if (g_adump) fwrite(zeros, 1, c, g_adump);
                      SDL_QueueAudio(adev, zeros, c); z -= c; }
                  g_fed += still;
              }
            }
            /* watchdog on PLAYED = fed - queued (advances even through silence). A
               reopen here only blocks THIS thread, not rendering. */
            unsigned long long played = g_fed - SDL_GetQueuedAudioSize(adev);
            Uint32 nowt = SDL_GetTicks(); if (!wd_t) wd_t = nowt;
            if (played != wd_played) { wd_played = played; wd_t = nowt; }
            else if (nowt - wd_t > 500) {
                SDL_CloseAudioDevice(adev); audio_open = 0; wd_t = nowt;
                fprintf(stderr, "viewer: audio device stalled -> reopening\n");
            }
            if (nowt - stat_t >= 4000) { stat_t = nowt;
                fprintf(stderr, "viewer audio: fed=%llu queued=%u ring=%u\n",
                        g_fed, SDL_GetQueuedAudioSize(adev), shm->a_write - shm->a_read); }
        }
        SDL_Delay(4);
    }
    if (audio_open) SDL_CloseAudioDevice(adev);
    return 0;
}

/* ---- window helpers (scale presets + fullscreen, used by F11/Alt-Enter and the menu) ---- */
static int g_cur_scale = 3, g_cur_fs = 0;
static int g_base_w = 320, g_base_h = 240;   /* logical game size (tracks mode changes) */
static void apply_scale(SDL_Window *win, int scale) {
    if (scale < 1) scale = 1;
    g_cur_scale = scale;
    if (g_cur_fs) { SDL_SetWindowFullscreen(win, 0); g_cur_fs = 0; }
    SDL_SetWindowSize(win, g_base_w * scale, g_base_h * scale);
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}
static void apply_fullscreen(SDL_Window *win, int on) {
    g_cur_fs = on;
    SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}
static void toggle_fullscreen(SDL_Window *win) { apply_fullscreen(win, !g_cur_fs); }

/* Forward declarations: these live in the Win32 menu section below, which in turn needs
   state_request() to be defined first. g_hwnd is the SDL window's HWND, kept at file scope so
   the slot picker can be opened from the hotkey chain as well as from the menu. */
#ifdef ME_WINMENU
static HWND g_hwnd;
static void prefs_save(void);
static void state_menu_refresh(void);
#endif


/* ---- on-screen toast -------------------------------------------------------------
 * The window title already carries PERSISTENT status ([REC]/[REPLAY]); this is for the other
 * kind: something the user must see once and then forget. A savestate confirmation has to be
 * transient and unmissable, and the title bar is neither, besides being invisible in fullscreen.
 * font8x8 and filled rects, which is exactly what settings_ui.c draws with, so no new dependency.
 */
static char   g_toast[128];
static Uint32 g_toast_t0 = 0;         /* SDL_GetTicks() when posted; 0 = nothing showing */
static Uint32 g_toast_ms = 0;
static int    g_toast_err = 0;
#define TOAST_MS      1900
#define TOAST_ERR_MS  3600

static void toast(int err, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_toast, sizeof g_toast, fmt, ap);
    va_end(ap);
    g_toast_err = err;
    g_toast_ms = err ? TOAST_ERR_MS : TOAST_MS;
    g_toast_t0 = SDL_GetTicks();
    if (!g_toast_t0) g_toast_t0 = 1;   /* 0 means "none", so never land on it */
}

static int toast_live(void) {
    if (!g_toast_t0) return 0;
    if (SDL_GetTicks() - g_toast_t0 >= g_toast_ms) { g_toast_t0 = 0; return 0; }
    return 1;
}

/* Draw over the frame, in GUEST pixel coordinates: the renderer's logical size is the guest
   screen, so a fixed pixel size here scales with the window like everything else. */
static void toast_render(SDL_Renderer *ren, int vw, int vh) {
    if (!toast_live()) return;
    Uint32 age = SDL_GetTicks() - g_toast_t0;
    int fade = (g_toast_ms - age) < 400 ? (int)((g_toast_ms - age) * 255 / 400) : 255;
    int px = vw >= 480 ? 2 : 1;                       /* one font pixel -> px guest pixels */
    int tw = (int)strlen(g_toast) * 8 * px, th = 8 * px;
    int pad = 4 * px;
    int x = (vw - tw) / 2, y = vh - th - pad * 3;
    if (x < pad) x = pad;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)(fade * 3 / 4));
    SDL_Rect bg = { x - pad, y - pad, tw + pad * 2, th + pad * 2 };
    SDL_RenderFillRect(ren, &bg);
    SDL_Color c = g_toast_err ? (SDL_Color){ 235, 90, 80, (Uint8)fade }
                             : (SDL_Color){ 235, 235, 235, (Uint8)fade };
    su_draw_text(ren, x, y, px, c, g_toast);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

/* ---- savestate requests ----------------------------------------------------------
 * In the bundle the viewer IS an engine thread, so it calls straight through. In the
 * two-process build it posts the request in the shm bytes reserved for it and the engine's
 * helper thread picks it up (see me_state_poll_request). The asymmetry is honest: the bundle
 * gets the engine's own reason string back, the two-process build gets a generic message,
 * because there is nowhere in the shm ABI to put a sentence and inventing somewhere would move
 * pixels[] and break every un-rebuilt guest shim.
 */
#define ME_STATE_REQ_SAVE 1
#define ME_STATE_REQ_LOAD 2

static int g_cur_slot = 1;            /* the slot F6/F7 cycle and Shift+F5 writes to */

static void state_request(int op, int slot) {
    char err[192] = {0};
#ifdef ME_BUNDLED
    const char *name = me_state_slot_name(slot);
    int rc = (op == ME_STATE_REQ_SAVE) ? me_state_save_slot(slot, err, sizeof err)
                                       : me_state_load_slot(slot, err, sizeof err);
    if (rc != 0) {
        toast(1, "%s failed: %s", op == ME_STATE_REQ_SAVE ? "Save" : "Load", err);
        return;
    }
    if (op == ME_STATE_REQ_SAVE) {
        toast(0, slot == ME_STATE_SLOT_QUICK ? "Quicksaved" : "Saved to slot %s", name);
#ifdef ME_WINMENU
        state_menu_refresh();       /* a filled slot stops being greyed out */
#endif
    } else {
        /* The load is queued to the engine main loop, so say so rather than claiming it is
           already done. How OLD the state is matters more than anything else here: loading the
           wrong one is the commonest savestate mistake. */
        char path[1024], age[64] = {0};
        if (me_state_slot_path_for_current(slot, path, sizeof path) == 0) {
            struct mst_info info;
            if (mst_probe(path, &info, NULL, NULL, NULL, NULL) == MST_OK) {
                long long secs = (long long)time(NULL) - (long long)info.save_time;
                if (secs < 0) secs = 0;
                if (secs < 90)          snprintf(age, sizeof age, "  (%llds ago)", secs);
                else if (secs < 5400)   snprintf(age, sizeof age, "  (%lldm ago)", secs / 60);
                else                    snprintf(age, sizeof age, "  (%lldh ago)", secs / 3600);
            }
        }
        toast(0, slot == ME_STATE_SLOT_QUICK ? "Loading quicksave%s" : "Loading slot %s%s",
              slot == ME_STATE_SLOT_QUICK ? age : name, slot == ME_STATE_SLOT_QUICK ? "" : age);
    }
#else
    if (!shm) return;
    if (shm->state_req) { toast(1, "Busy"); return; }
    shm->state_slot = (uint8_t)slot;      /* slot FIRST: state_req is the publish point */
    shm->state_req  = (uint8_t)op;
    toast(0, op == ME_STATE_REQ_SAVE ? "Saving slot %s..." : "Loading slot %s...",
          me_state_slot_name(slot));
    (void)err;
#endif
}

static void state_cycle_slot(int d) {
    g_cur_slot += d;
    if (g_cur_slot < 1) g_cur_slot = ME_STATE_NSLOTS;
    if (g_cur_slot > ME_STATE_NSLOTS) g_cur_slot = 1;
#ifdef ME_WINMENU
    prefs_save();               /* the chosen slot survives a restart */
    state_menu_refresh();
#endif
    toast(0, "Slot %d selected", g_cur_slot);
}

/* The picker calls back into state_request so it and the hotkeys take exactly one path. */
static void state_picker_save(int slot) { state_request(ME_STATE_REQ_SAVE, slot); }
static void state_picker_load(int slot) { state_request(ME_STATE_REQ_LOAD, slot); }

static void state_picker_open(void) {
#ifdef ME_WINMENU
    state_win_open(g_hwnd, g_cur_slot, state_picker_save, state_picker_load);
#else
    /* No native toolkit here, so the picker is a page of the in-SDL overlay. */
    su_open_page(&g_su, (int)(shm ? shm->device : 0), SU_PAGE_STATES);
#endif
}
#ifdef ME_WINMENU
/* ---- Win32 menu bar (bundle only: File->Open hot-reloads via the in-process engine) ---- */
enum { IDM_OPEN = 1001, IDM_RELOAD, IDM_EXIT,
       IDM_SCALE1 = 1010, IDM_SCALE2, IDM_SCALE3, IDM_SCALE4, IDM_FULLSCREEN, IDM_SCREENSHOT,
       IDM_ABOUT = 1030, IDM_SETTINGS,
       IDM_RECORD = 1035,
       IDM_FW_INSTALL = 1040, IDM_FW_WIZ, IDM_FW_CAANOO, IDM_FW_F100, IDM_FW_F200,
       IDM_PATHS = 1050,
       IDM_STATE_SAVE = 1060, IDM_STATE_LOAD, IDM_STATE_SAVESLOT, IDM_STATE_PICKER,
       IDM_STATE_SLOT0 = 1070,   /* .. IDM_STATE_SLOT0 + ME_STATE_NSLOTS */
       IDM_RECENT0 = 1100 };
#define MAX_RECENT 8
static HMENU g_recentmenu;
static char g_recent[MAX_RECENT][MAX_PATH]; static int g_nrecent;
static char g_last_game[MAX_PATH];
static int g_shot_w = 320, g_shot_h = 240;   /* current present dims, for Video > Screenshot */

static void recent_path(char *out, size_t cap) {
    char dir[MAX_PATH]; me_paths_dir(ME_PATH_SETTINGS, dir, sizeof dir);   /* portable Settings dir */
    snprintf(out, cap, "%s\\recent.txt", dir);
}
static void recent_load(void) {
    g_nrecent = 0; char p[MAX_PATH]; recent_path(p, sizeof p);
    FILE *f = fopen(p, "r"); if (!f) return;
    while (g_nrecent < MAX_RECENT && fgets(g_recent[g_nrecent], MAX_PATH, f)) {
        char *nl = strpbrk(g_recent[g_nrecent], "\r\n"); if (nl) *nl = 0;
        if (g_recent[g_nrecent][0]) g_nrecent++;
    }
    fclose(f);
}
static void recent_save(void) {
    char p[MAX_PATH]; recent_path(p, sizeof p);
    FILE *f = fopen(p, "w"); if (!f) return;
    for (int i = 0; i < g_nrecent; i++) fprintf(f, "%s\n", g_recent[i]);
    fclose(f);
}
static void recent_add(const char *path) {
    char tmp[MAX_RECENT][MAX_PATH]; int n = 0;
    snprintf(tmp[n++], MAX_PATH, "%s", path);
    for (int i = 0; i < g_nrecent && n < MAX_RECENT; i++)
        if (strcmp(g_recent[i], path)) snprintf(tmp[n++], MAX_PATH, "%s", g_recent[i]);
    memcpy(g_recent, tmp, sizeof tmp); g_nrecent = n; recent_save();
}
static void recent_rebuild_menu(void) {
    if (!g_recentmenu) return;
    while (GetMenuItemCount(g_recentmenu) > 0) DeleteMenu(g_recentmenu, 0, MF_BYPOSITION);
    if (!g_nrecent) { AppendMenuA(g_recentmenu, MF_STRING | MF_GRAYED, 0, "(none)"); return; }
    for (int i = 0; i < g_nrecent; i++) {
        const char *b = strrchr(g_recent[i], '\\'); b = b ? b + 1 : g_recent[i];
        AppendMenuA(g_recentmenu, MF_STRING, IDM_RECENT0 + i, b);
    }
}
/* ---- games folder: persisted + exported as ME_GP2X_SD (mapped to /mnt/sd & /mnt/nand) ---- */
static char g_games_dir[MAX_PATH];
static void games_path(char *out, size_t cap) {
    char dir[MAX_PATH]; me_paths_dir(ME_PATH_SETTINGS, dir, sizeof dir);   /* portable Settings dir */
    snprintf(out, cap, "%s\\games.txt", dir);
}
static void games_load(void) {
    char p[MAX_PATH]; games_path(p, sizeof p);
    FILE *f = fopen(p, "r"); if (!f) return;
    if (fgets(g_games_dir, sizeof g_games_dir, f)) { char *nl = strpbrk(g_games_dir, "\r\n"); if (nl) *nl = 0; }
    fclose(f);
    if (g_games_dir[0]) _putenv_s("ME_GP2X_SD", g_games_dir);
}
/* ---- persisted app prefs (volume / mute / confirm-on-exit) in the Settings dir ---------------- */
static int g_confirm_exit = 1;   /* Esc + window-close ask before quitting unless disabled */
static void prefs_path(char *out, size_t cap) {
    char dir[MAX_PATH]; me_paths_dir(ME_PATH_SETTINGS, dir, sizeof dir);
    snprintf(out, cap, "%s\\prefs.txt", dir);
}
static void prefs_save(void) {
    char p[MAX_PATH]; prefs_path(p, sizeof p);
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "volume=%d\nmute=%d\nconfirm_exit=%d\n", g_view_volume, g_view_mute, g_confirm_exit);
    fclose(f);
}
static void prefs_load(void) {
    char p[MAX_PATH]; prefs_path(p, sizeof p);
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='); if (!eq) continue; *eq = 0;
        int v = atoi(eq + 1);
        if (!strcmp(line, "volume"))            { if (v < 0) v = 0; if (v > 100) v = 100; g_view_volume = v; }
        else if (!strcmp(line, "mute"))         g_view_mute = !!v;
        else if (!strcmp(line, "confirm_exit")) g_confirm_exit = !!v;
        /* The chosen savestate slot is a UI cursor, not save data -- one global setting, so
           F6/F7 and Shift+F5 behave the same way whichever game is loaded. */
        else if (!strcmp(line, "state_slot"))   { if (v >= 1 && v <= ME_STATE_NSLOTS) g_cur_slot = v; }
    }
    fclose(f);
}
/* Accessors for the Audio tab of File > Settings (paths_win.c); changes persist immediately. */
int  me_view_get_volume(void) { return g_view_volume; }
int  me_view_get_mute(void)   { return g_view_mute; }
void me_view_set_volume(int v) { if (v < 0) v = 0; if (v > 100) v = 100; g_view_volume = v; prefs_save(); }
void me_view_set_mute(int on)  { g_view_mute = !!on; prefs_save(); }

/* ---- confirm-exit dialog (Esc / window close) with a "Don't ask again" checkbox -------------
   A tiny modal child window + local message loop -- MinGW has no resource-compiled dialogs, so we
   build the controls by hand (same pattern as paths_win.c). Returns 1 to quit, 0 to stay. */
enum { IDC_CE_DONTASK = 4001 };
static int  s_ce_result;   /* -1 pending, 0 stay, 1 quit */
static HWND s_ce_chk;
static LRESULT CALLBACK ce_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK)     { s_ce_result = 1; return 0; }
        if (LOWORD(wp) == IDCANCEL) { s_ce_result = 0; return 0; }
        break;
    case WM_CLOSE: s_ce_result = 0; return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}
static int confirm_exit_dialog(HWND parent) {
    HINSTANCE inst = (HINSTANCE)GetModuleHandle(NULL);
    static int reg = 0;
    if (!reg) { reg = 1;
        WNDCLASSA wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = ce_proc; wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "MagiceyesConfirm";
        RegisterClassA(&wc);
    }
    const int CW = 360, CH = 158;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT pr; int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (parent && GetWindowRect(parent, &pr)) {
        x = pr.left + ((pr.right - pr.left) - CW) / 2;
        y = pr.top  + ((pr.bottom - pr.top) - CH) / 2;
    }
    HWND dlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "MagiceyesConfirm", "magiceyes", style,
                               x, y, CW, CH, parent, NULL, inst, NULL);
    if (!dlg) return 1;   /* can't build the dialog -> just quit */
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    CreateWindowA("STATIC", "Quit magiceyes?", WS_CHILD | WS_VISIBLE,
                  20, 18, CW - 40, 20, dlg, NULL, inst, NULL);
    s_ce_chk = CreateWindowA("BUTTON", "Don't ask me again",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                  20, 46, CW - 40, 22, dlg, (HMENU)(INT_PTR)IDC_CE_DONTASK, inst, NULL);
    HWND bq = CreateWindowA("BUTTON", "Quit", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                  CW - 200, CH - 64, 84, 28, dlg, (HMENU)(INT_PTR)IDOK, inst, NULL);
    CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                  CW - 104, CH - 64, 84, 28, dlg, (HMENU)(INT_PTR)IDCANCEL, inst, NULL);
    for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageA(c, WM_SETFONT, (WPARAM)font, TRUE);

    s_ce_result = -1;
    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW); SetForegroundWindow(dlg); SetFocus(bq);
    MSG msg;
    while (s_ce_result < 0 && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    int quit    = (s_ce_result == 1);
    int dontask = (SendMessageA(s_ce_chk, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (parent) EnableWindow(parent, TRUE);
    DestroyWindow(dlg); s_ce_chk = NULL;
    if (quit && dontask) { g_confirm_exit = 0; prefs_save(); }
    return quit;
}
/* Esc / close-box / Exit menu funnel here: honour the saved "don't ask" choice. */
static int request_exit(HWND hwnd) { return g_confirm_exit ? confirm_exit_dialog(hwnd) : 1; }

/* a device is "installed" if its staged gp2xmenu exists */
static int fw_installed(const char *dev) { char r[1024], m[1024]; return me_firmware_paths(dev, r, m, sizeof r); }

/* The State menu's own handles, so a save or a slot change can refresh it in place. A whole
   rebuild via build_menu() would work too, but it also reloads the recent list and the games
   folder, which is a lot of work for "slot 3 is no longer empty". */
static HMENU g_statemenu, g_slotmenu;

static int slot_has_state(int slot) {
    char path[1024];
    if (me_state_slot_path_for_current(slot, path, sizeof path) != 0) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Grey what cannot be done, and name the slot Shift+F5 would write to. Same idea as
   fw_installed() greying the un-installed firmware entries. */
static void state_menu_refresh(void) {
    if (!g_statemenu) return;
    int have_game = g_last_game[0] != 0;
    char label[64];
    snprintf(label, sizeof label, "&Save to Slot %d\tShift+F5", g_cur_slot);
    ModifyMenuA(g_statemenu, IDM_STATE_SAVESLOT, MF_BYCOMMAND | MF_STRING, IDM_STATE_SAVESLOT, label);
    EnableMenuItem(g_statemenu, IDM_STATE_SAVE, MF_BYCOMMAND | (have_game ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(g_statemenu, IDM_STATE_SAVESLOT, MF_BYCOMMAND | (have_game ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(g_statemenu, IDM_STATE_LOAD, MF_BYCOMMAND |
                   ((have_game && slot_has_state(ME_STATE_SLOT_QUICK)) ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(g_statemenu, IDM_STATE_PICKER, MF_BYCOMMAND | (have_game ? MF_ENABLED : MF_GRAYED));
    if (g_slotmenu) {
        for (int s = 1; s <= ME_STATE_NSLOTS; s++) {
            char t[32];
            snprintf(t, sizeof t, "Slot &%d%s", s, slot_has_state(s) ? "" : "\t(empty)");
            ModifyMenuA(g_slotmenu, IDM_STATE_SLOT0 + s, MF_BYCOMMAND | MF_STRING,
                        IDM_STATE_SLOT0 + s, t);
        }
        CheckMenuRadioItem(g_slotmenu, IDM_STATE_SLOT0 + 1, IDM_STATE_SLOT0 + ME_STATE_NSLOTS,
                           IDM_STATE_SLOT0 + g_cur_slot, MF_BYCOMMAND);
    }
    DrawMenuBar(g_hwnd);
}

static void build_menu(HWND hwnd) {
    HMENU bar = CreateMenu(), file = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, IDM_OPEN, "&Open...\tCtrl+O");
    g_recentmenu = CreatePopupMenu();
    AppendMenuA(file, MF_POPUP, (UINT_PTR)g_recentmenu, "Open &Recent");
    AppendMenuA(file, MF_STRING, IDM_RELOAD, "&Reload");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, IDM_PATHS, "&Settings...");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, IDM_EXIT, "E&xit");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&File");
    HMENU video = CreatePopupMenu();
    AppendMenuA(video, MF_STRING, IDM_SCALE1, "Scale &1x");
    AppendMenuA(video, MF_STRING, IDM_SCALE2, "Scale &2x");
    AppendMenuA(video, MF_STRING, IDM_SCALE3, "Scale &3x");
    AppendMenuA(video, MF_STRING, IDM_SCALE4, "Scale &4x");
    AppendMenuA(video, MF_SEPARATOR, 0, NULL);
    AppendMenuA(video, MF_STRING, IDM_FULLSCREEN, "&Fullscreen\tF11");
    AppendMenuA(video, MF_STRING, IDM_SCREENSHOT, "&Screenshot\tF12");
    AppendMenuA(video, MF_SEPARATOR, 0, NULL);
    AppendMenuA(video, MF_STRING, IDM_RECORD, "&Record input\tF9");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)video, "&Video");
    /* State gets its own top-level menu rather than a corner of File. The bar is organised by
       subsystem (Input is a single item and still earned a popup), and these four verbs plus a
       ten-entry slot submenu would make File the only oversized menu -- with "Quick Save" sitting
       next to Open Recent and Exit, which are about the application, not the running game. */
    g_statemenu = CreatePopupMenu();
    AppendMenuA(g_statemenu, MF_STRING, IDM_STATE_SAVE, "&Quick Save\tF5");
    AppendMenuA(g_statemenu, MF_STRING, IDM_STATE_LOAD, "Quick &Load\tF8");
    AppendMenuA(g_statemenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_statemenu, MF_STRING, IDM_STATE_SAVESLOT, "&Save to Slot 1\tShift+F5");
    g_slotmenu = CreatePopupMenu();
    for (int s = 1; s <= ME_STATE_NSLOTS; s++) {
        char t[32]; snprintf(t, sizeof t, "Slot &%d", s);
        AppendMenuA(g_slotmenu, MF_STRING, IDM_STATE_SLOT0 + s, t);
    }
    AppendMenuA(g_statemenu, MF_POPUP, (UINT_PTR)g_slotmenu, "&Current Slot");
    AppendMenuA(g_statemenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_statemenu, MF_STRING, IDM_STATE_PICKER, "&Manage States...\tF4");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)g_statemenu, "&State");
    HMENU fw = CreatePopupMenu();
    AppendMenuA(fw, MF_STRING, IDM_FW_INSTALL, "&Install firmware...");
    AppendMenuA(fw, MF_SEPARATOR, 0, NULL);
    AppendMenuA(fw, MF_STRING | (fw_installed("wiz")    ? 0 : MF_GRAYED), IDM_FW_WIZ,    "Boot &Wiz menu");
    AppendMenuA(fw, MF_STRING | (fw_installed("caanoo") ? 0 : MF_GRAYED), IDM_FW_CAANOO, "Boot &Caanoo menu");
    AppendMenuA(fw, MF_STRING | (fw_installed("f100")   ? 0 : MF_GRAYED), IDM_FW_F100,   "Boot GP2X &F100 menu");
    AppendMenuA(fw, MF_STRING | (fw_installed("f200")   ? 0 : MF_GRAYED), IDM_FW_F200,   "Boot GP2X F&200 menu");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)fw, "Fir&mware");
    HMENU inp = CreatePopupMenu();
    AppendMenuA(inp, MF_STRING, IDM_SETTINGS, "&Keybindings...\tF1");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)inp, "&Input");
    HMENU help = CreatePopupMenu();
    AppendMenuA(help, MF_STRING, IDM_ABOUT, "&About");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)help, "&Help");
    recent_load(); recent_rebuild_menu();
    games_load();
    SetMenu(hwnd, bar);
    state_menu_refresh();   /* grey what is not available yet */
}
static void start_game(const char *path) {
    char bin[MAX_PATH * 2];
    const char *r = resolve_input(path, bin, sizeof bin);
    if (!r) { MessageBoxA(NULL, "No .gpe found, or the folder/zip is ambiguous.\nSee the console for details.",
                          "magiceyes", MB_ICONERROR); return; }
    int c = classify_elf(r);
    if (c == 1) {   /* dynamically-linked title (Odonata, Wind & Water, Caanoo GLES, Wiz...): pick the
                       rootfs whose linker it actually needs -- ld-linux.so.2 (firmware glibc) vs .so.3
                       (EABI). Mirrors the CLI; a hardcoded .so.2 probe wrongly bailed on EABI titles
                       (and on any setup where the default rootfs is the EABI one). load_elf re-selects. */
        char interp[256];
        if (!read_elf_interp(r, interp, sizeof interp))
            snprintf(interp, sizeof interp, "/lib/ld-linux.so.2");
        if (!me_rootfs_select(interp)) {
            if (strstr(interp, "ld-linux.so.3"))   /* EABI runtime ships with magiceyes */
                MessageBoxA(NULL, "This title needs the EABI runtime that ships with magiceyes, but it\n"
                                  "appears to be missing. Reinstall magiceyes (it bundles rootfs-eabi\n"
                                  "next to the .exe).", "magiceyes", MB_ICONWARNING);
            else                                    /* OABI: Wiz or GP2X firmware */
                MessageBoxA(NULL, "This is a Wiz/GP2X title that needs the device's system libraries\n"
                                  "from official firmware.\n\n"
                                  "Install it from the Firmware menu > Install firmware (a Wiz, or\n"
                                  "GP2X F100/F200, firmware .zip/.img), then open the title again.\n\n"
                                  "If it is a Wiz title and the wrong firmware is picked, set\n"
                                  "MAGICEYES_DEVICE=wiz.", "magiceyes", MB_ICONWARNING);
            return;
        }
    }
    if (c < 0) { MessageBoxA(NULL, "Not a usable GP2X ARM binary.\nSee the console for details.",
                            "magiceyes", MB_ICONERROR); return; }
    snprintf(g_last_game, sizeof g_last_game, "%s", path);
    recent_add(path); recent_rebuild_menu();
    if (kbwin_is_open()) kbwin_close();                /* leaving the keybind editor to load a game */
    if (paths_win_is_open()) paths_win_close();        /* ...and the storage-folder editor */
    if (su_is_open(&g_su)) su_close_and_save(&g_su);
    engine_request_reload(r);   /* engine stops the current game + hot-loads this one */
}
static void do_open_dialog(HWND hwnd) {
    wchar_t buf[MAX_PATH]; buf[0] = 0;
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"GP2X games (*.gpe;*.zip)\0*.gpe;*.zip\0ELF binaries (*.elf)\0*.elf\0All files\0*.*\0";
    ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Open GP2X game";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    char path[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, path, sizeof path, NULL, NULL);
    start_game(path);
}
static void do_install_firmware(HWND hwnd) {
    wchar_t buf[MAX_PATH]; buf[0] = 0;
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Device firmware (*.zip;*.img)\0*.zip;*.img\0All files\0*.*\0";
    ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Install device firmware";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    char path[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, path, sizeof path, NULL, NULL);
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    int rc = me_firmware_install(path, NULL);   /* device detected from the firmware contents */
    SetCursor(old);
    if (rc == 0) {
        MessageBoxA(hwnd, "Firmware installed. Boot it from the Firmware menu.", "magiceyes", MB_OK);
        build_menu(hwnd);   /* refresh: enable the newly-installed device's Boot item */
    } else {
        MessageBoxA(hwnd, "Could not install that firmware (unrecognised or unsupported format).\n"
                          "See the log for details.", "magiceyes", MB_ICONERROR);
    }
}
static void boot_firmware(HWND hwnd, const char *dev) {
    if (!me_firmware_boot_request(dev))
        MessageBoxA(hwnd, "That firmware isn't installed yet.\nUse Firmware > Install firmware...",
                    "magiceyes", MB_ICONWARNING);
}
static void input_record_toggle(void);   /* defined with the record/replay module below */
static void save_screenshot(int w, int h);   /* defined below (Video > Screenshot / F12) */
static void handle_menu_command(SDL_Window *win, HWND hwnd, int id) {
    switch (id) {
    case IDM_OPEN:   do_open_dialog(hwnd); break;
    case IDM_RECORD: input_record_toggle(); break;
    case IDM_RELOAD: if (g_last_game[0]) start_game(g_last_game); break;
    case IDM_EXIT:   { SDL_Event q; memset(&q, 0, sizeof q); q.type = SDL_QUIT; SDL_PushEvent(&q); } break;
    case IDM_SCALE1: apply_scale(win, 1); break;
    case IDM_SCALE2: apply_scale(win, 2); break;
    case IDM_SCALE3: apply_scale(win, 3); break;
    case IDM_SCALE4: apply_scale(win, 4); break;
    case IDM_FULLSCREEN: toggle_fullscreen(win); break;
    case IDM_SCREENSHOT: save_screenshot(g_shot_w, g_shot_h); break;
    case IDM_SETTINGS:   kbwin_open(hwnd, &g_ic, (int)shm->device); break;
    case IDM_FW_INSTALL: do_install_firmware(hwnd); break;
    case IDM_FW_WIZ:     boot_firmware(hwnd, "wiz"); break;
    case IDM_FW_CAANOO:  boot_firmware(hwnd, "caanoo"); break;
    case IDM_FW_F100:    boot_firmware(hwnd, "f100"); break;
    case IDM_FW_F200:    boot_firmware(hwnd, "f200"); break;
    case IDM_PATHS:      paths_win_open(hwnd); break;
    case IDM_STATE_SAVE:     state_request(ME_STATE_REQ_SAVE, ME_STATE_SLOT_QUICK); break;
    case IDM_STATE_LOAD:     state_request(ME_STATE_REQ_LOAD, ME_STATE_SLOT_QUICK); break;
    case IDM_STATE_SAVESLOT: state_request(ME_STATE_REQ_SAVE, g_cur_slot); break;
    case IDM_STATE_PICKER:   state_picker_open(); break;
    case IDM_ABOUT:
        MessageBoxA(hwnd,
                    "magiceyes"
#ifdef ME_VERSION
                    "  " ME_VERSION
#endif
                    "\nRun Game Park GP2X, Wiz, and Caanoo games on a PC.\n\n"
                    "Copyright (c) 2026 Zach Diemer\n"
                    "github.com/zdiemer/magiceyes",
                    "About magiceyes", MB_OK); break;
    default:
        if (id > IDM_STATE_SLOT0 && id <= IDM_STATE_SLOT0 + ME_STATE_NSLOTS) {
            g_cur_slot = id - IDM_STATE_SLOT0;
            prefs_save();
            state_menu_refresh();
            toast(0, "Slot %d selected", g_cur_slot);
        } else if (id >= IDM_RECENT0 && id < IDM_RECENT0 + MAX_RECENT) {
            int i = id - IDM_RECENT0; if (i < g_nrecent) start_game(g_recent[i]);
        }
    }
}
#endif /* ME_WINMENU */

/* ---- Screenshot (F12): grab the live RGB565 framebuffer -> screenshots/magiceyes_<N>.png ---- */
#include "png_write.h"
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#endif

/* Directory the executable lives in (so screenshots land in a predictable place, not the game's
   working dir). Returns 0 on success. */
static int exe_dir(char *out, size_t cap) {
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    if (n == 0 || n >= cap) return -1;
    char *a = strrchr(out, '\\'), *b = strrchr(out, '/'), *s = a > b ? a : b;
    if (!s) return -1;
    *s = 0;
    return 0;
#else
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0) return -1;
    out[n] = 0;
    char *s = strrchr(out, '/');
    if (!s) return -1;
    *s = 0;
    return 0;
#endif
}

static void save_screenshot(int w, int h) {
    if (w <= 0 || h <= 0) return;
    uint8_t *rgb = (uint8_t *)malloc((size_t)w * h * 3);
    if (!rgb) return;
    /* RGB565 -> RGB888 (bit-replicate the low bits), top-to-bottom; shm rows are GP2XSHM_MAXW wide. */
    const uint16_t *src = (const uint16_t *)shm->pixels;
    for (int y = 0; y < h; y++) {
        const uint16_t *row = src + (size_t)y * GP2XSHM_MAXW;
        uint8_t *d = rgb + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            uint16_t v = row[x];
            unsigned r5 = (v >> 11) & 0x1f, g6 = (v >> 5) & 0x3f, b5 = v & 0x1f;
            *d++ = (r5 << 3) | (r5 >> 2);
            *d++ = (g6 << 2) | (g6 >> 4);
            *d++ = (b5 << 3) | (b5 >> 2);
        }
    }
    char dir[1024], sdir[1100], path[1200];
    if (exe_dir(dir, sizeof dir) != 0) snprintf(dir, sizeof dir, ".");
    snprintf(sdir, sizeof sdir, "%s/screenshots", dir);
#ifdef _WIN32
    _mkdir(sdir);
#else
    mkdir(sdir, 0755);
#endif
    /* Next free magiceyes_<N>.png -- never overwrite, and no wall-clock needed. */
    static int next = 0;
    int rc = -1;
    for (int tries = 0; tries < 100000; tries++) {
        snprintf(path, sizeof path, "%s/magiceyes_%d.png", sdir, next++);
        FILE *probe = fopen(path, "rb");
        if (probe) { fclose(probe); continue; }   /* already exists -> skip */
        rc = png_write_rgb(path, rgb, w, h);
        break;
    }
    free(rgb);
    fprintf(stderr, rc == 0 ? "[screenshot] saved %s\n" : "[screenshot] FAILED to write %s\n", path);
}

/* ---- input record / replay -------------------------------------------------
 * A recording is "<frame_seq> <buttons_hex>" lines (one per button-state CHANGE), keyed to the
 * shim's frame counter. Record live (F9 / Record menu / ME_INPUT_RECORD=<path>), replay with
 * ME_INPUT_REPLAY=<path>. For the replay to be DETERMINISTIC across hosts (so a recording is a
 * stable regression input), run with FAKESDL_VTIME=<fps> so the game's clock advances per-frame
 * rather than by wall-clock; otherwise a given frame_seq maps to slightly different game state on
 * a slower/faster machine. Shared, line-for-line, with the headless harness (tools/test). */
static FILE *g_rec = NULL; static uint32_t g_rec_prevb = 0; static char g_rec_path[1024];
static int g_rec_ptx = -1, g_rec_pty = -1, g_rec_ptd = -1;   /* last-recorded touch state */
/* recording lines: "<frame> <buttons_hex>" (button change) and "T <frame> <x> <y> <down>" (touch
   change). x/y are guest pixels; down is 0/1. The bare-number line is buttons (back-compatible). */
struct rep_ev { uint32_t frame; char type; uint32_t btn; int x, y, down; };
static struct rep_ev *g_rep = NULL;
static int g_rep_n = 0, g_rep_i = 0, g_rep_on = 0, g_rep_done = 0;
static uint32_t g_rep_btn = 0; static int g_rep_tx = 0, g_rep_ty = 0, g_rep_td = 0;  /* held state */

static void input_record_close(void) {
    if (g_rec) { fclose(g_rec); g_rec = NULL; me_log("[fw] viewer: saved recording %s\n", g_rec_path); }
}
static void input_record_open(const char *path) {
    input_record_close();
    g_rec = fopen(path, "w");
    if (!g_rec) { me_log("[fw] viewer: record: can't write %s\n", path); return; }
    snprintf(g_rec_path, sizeof g_rec_path, "%s", path);
    fprintf(g_rec, "# magiceyes-input v1\n");
    g_rec_prevb = ~0u; g_rec_ptx = g_rec_pty = -999999; g_rec_ptd = -1;  /* force the opening state */
    me_log("[fw] viewer: recording input -> %s\n", path);
}
/* F9 / menu: toggle recording to "<game-basename>.rec" in the cwd (= the game's dir). */
static void input_record_toggle(void) {
    if (g_rec) { input_record_close(); return; }
    const char *g = g_cur_game[0] ? g_cur_game : "recording";
    const char *s1 = strrchr(g, '/'), *s2 = strrchr(g, '\\'), *s = s1 > s2 ? s1 : s2;
    char name[300]; snprintf(name, sizeof name, "%s", s ? s + 1 : g);
    char *dot = strrchr(name, '.'); if (dot) *dot = 0;
    char path[512]; snprintf(path, sizeof path, "%s.rec", name);
    input_record_open(path);
}
static void input_replay_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { me_log("[fw] viewer: replay: can't open %s\n", path); return; }
    int cap = 64; g_rep = malloc((size_t)cap * sizeof *g_rep); g_rep_n = 0;
    char line[160];
    while (g_rep && fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        struct rep_ev e; memset(&e, 0, sizeof e);
        unsigned fr, bt;
        if ((line[0] == 'T' || line[0] == 't') &&
            sscanf(line + 1, "%u %d %d %d", &fr, &e.x, &e.y, &e.down) == 4) { e.type = 'T'; e.frame = fr; }
        else if (sscanf(line, "%u %x", &fr, &bt) == 2) { e.type = 'B'; e.frame = fr; e.btn = bt; }
        else continue;
        if (g_rep_n >= cap) { cap *= 2; g_rep = realloc(g_rep, (size_t)cap * sizeof *g_rep); if (!g_rep) break; }
        g_rep[g_rep_n++] = e;
    }
    fclose(f);
    g_rep_on = g_rep && g_rep_n > 0;
    me_log("[fw] viewer: replay %d events from %s (use FAKESDL_VTIME for determinism)\n", g_rep_n, path);
}
/* Per-frame: apply replayed input (buttons + touch via *tx/*ty/*td) at this frame_seq, or record
   changes of the live keyboard+touch. Returns the button bitmap to apply. */
static uint32_t input_step(gp2x_shm_t *shm, uint32_t kbd, int *tx, int *ty, int *td) {
    uint32_t f = shm->frame_seq;
    if (g_rep_on) {
        while (g_rep_i < g_rep_n && g_rep[g_rep_i].frame <= f) {
            struct rep_ev *e = &g_rep[g_rep_i++];
            if (e->type == 'T') { g_rep_tx = e->x; g_rep_ty = e->y; g_rep_td = e->down; }
            else g_rep_btn = e->btn;
        }
        *tx = g_rep_tx; *ty = g_rep_ty; *td = g_rep_td;
        if (g_rep_i >= g_rep_n && !g_rep_done) { g_rep_done = 1;
            me_log("[fw] viewer: replay complete at frame %u\n", f); }
        return g_rep_btn;
    }
    if (g_rec) {
        if (kbd != g_rec_prevb) { fprintf(g_rec, "%u %08x\n", f, kbd); g_rec_prevb = kbd; }
        if (*tx != g_rec_ptx || *ty != g_rec_pty || *td != g_rec_ptd) {
            fprintf(g_rec, "T %u %d %d %d\n", f, *tx, *ty, *td);
            g_rec_ptx = *tx; g_rec_pty = *ty; g_rec_ptd = *td;
        }
        fflush(g_rec);
    }
    return kbd;
}

/* Run the viewer on an already-mapped shm. In the two-process build `main` maps the shm and
   calls this; in the single-process bundle the engine passes its in-process g_shm here from a
   worker thread (host/engine/main.c) -- same pointer the engine writes, so input/audio/frames
   need no transport. */
int viewer_run(gp2x_shm_t *shm_in, int scale, int fullscreen, int mute, int volume) {
    shm = shm_in;
    if (scale < 1) scale = 1;
    g_view_mute = mute; g_view_volume = volume;
    g_cur_scale = scale; g_cur_fs = fullscreen;
#ifdef ME_BUNDLED
    SDL_SetMainReady();                /* required when SDL_MAIN_HANDLED is set */
#endif

#ifndef _WIN32
    /* When PULSE_SERVER is set the box is running PulseAudio (possibly over a
       socket, e.g. a remote/containerised display); SDL may otherwise default to
       a backend (ALSA) with no usable device and fail to open. Prefer PulseAudio
       in that case, unless the user pinned SDL_AUDIODRIVER. Harmless on a normal
       desktop, where PULSE_SERVER is usually unset and SDL autodetects. (Linux-only:
       PULSE_SERVER is never set on Windows, and MinGW has no setenv.) */
    if (getenv("PULSE_SERVER") && !getenv("SDL_AUDIODRIVER")) {
        setenv("SDL_AUDIODRIVER", "pulseaudio", 1);
    }
    /* Request a generous PulseAudio server buffer; some setups otherwise drop the
       stream under latency spikes. */
    if (!getenv("PULSE_LATENCY_MSEC")) setenv("PULSE_LATENCY_MSEC", "120", 1);
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    int w = shm->width ? (int)shm->width : 320;
    int h = shm->height ? (int)shm->height : 240;
    g_base_w = w; g_base_h = h;
    Uint32 wflags = SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_Window *win = SDL_CreateWindow("magiceyes",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w * scale, h * scale, wflags);
    /* Prefer a GPU-accelerated renderer so the per-frame scale/present is offloaded to the GPU
       (on a low-spec machine a software renderer's CPU present competes with the emulator). Fall
       back through accelerated-without-vsync, then any renderer, if the GPU path is unavailable. */
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);   /* black clear/letterbox (avoid an unpainted strip) */
    SDL_RenderSetLogicalSize(ren, w, h);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, w, h);
    int cur_w = w, cur_h = h;
#ifdef ME_WINMENU
    g_shot_w = cur_w; g_shot_h = cur_h;   /* keep the Video>Screenshot menu in sync with present dims */
#endif
#ifdef ME_BUNDLED
    if (g_fwlog) {
        int nd = SDL_GetNumRenderDrivers();
        for (int i = 0; i < nd; i++) { SDL_RendererInfo di;
            if (SDL_GetRenderDriverInfo(i, &di) == 0)
                me_log("[fw] viewer: available driver[%d]='%s' flags=%x\n", i, di.name, di.flags); }
        me_log("[fw] viewer: (override with env SDL_RENDER_DRIVER=software|opengl|direct3d11)\n");
        SDL_RendererInfo ri;
        if (ren && SDL_GetRendererInfo(ren, &ri) == 0) {
            int has565 = 0;
            for (Uint32 i = 0; i < ri.num_texture_formats; i++)
                if (ri.texture_formats[i] == SDL_PIXELFORMAT_RGB565) has565 = 1;
            me_log("[fw] viewer: renderer='%s' flags=%x RGB565=%d maxtex=%dx%d initial-tex=%p err='%s'\n",
                   ri.name, ri.flags, has565, ri.max_texture_width, ri.max_texture_height,
                   (void *)tex, SDL_GetError());
        } else me_log("[fw] viewer: SDL_GetRendererInfo failed (ren=%p) err='%s'\n", (void *)ren, SDL_GetError());
    }
#endif

#ifdef ME_WINMENU
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); HWND hwnd = NULL;
    if (SDL_GetWindowWMInfo(win, &wmi)) hwnd = wmi.info.win.window;
    g_hwnd = hwnd;
    if (hwnd) {
        build_menu(hwnd);
        if (!fullscreen) SDL_SetWindowSize(win, w * scale, h * scale);  /* regrow past the menu bar */
    }
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);   /* deliver WM_COMMAND to SDL_PollEvent */
#endif

    SDL_Thread *ath = SDL_CreateThread(audio_thread, "gp2x-audio", NULL);
    uint32_t last_seq = ~0u;
    int running = 1;
    int touch_x = 0, touch_y = 0, touch_down = 0;   /* touchscreen, in guest pixels */

    /* input bindings (defaults + bindings.conf) + the keybind settings overlay. Done after
       SDL_Init so the scancode/controller name APIs the loader uses are available. Open any
       controllers already attached; hotplug is handled in the event loop. */
#ifdef ME_WINMENU
    { char cfg[MAX_PATH]; me_paths_dir(ME_PATH_SETTINGS, cfg, sizeof cfg); ic_set_config_dir(cfg); }
    prefs_load();   /* persisted volume / mute / confirm-on-exit (GUI prefs win over CLI defaults) */
#endif
    ic_load(&g_ic);
    su_init(&g_su, &g_ic);
    /* the States page posts through the same request path the hotkeys use */
    su_set_state_hooks(&g_su, state_picker_save, state_picker_load);
    for (int i = 0; i < SDL_NumJoysticks() && g_npads < ME_MAXPADS; i++)
        if (SDL_IsGameController(i)) { SDL_GameController *c = SDL_GameControllerOpen(i);
            if (c) g_pads[g_npads++] = c; }

    { const char *rp = getenv("ME_INPUT_RECORD"); if (rp && *rp) input_record_open(rp);
      const char *vp = getenv("ME_INPUT_REPLAY"); if (vp && *vp) input_replay_load(vp); }

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            /* window close (X / Alt+F4 / File>Exit) + controller hotplug processed regardless of
               the overlay. The close box routes through request_exit (confirm unless disabled).
               Handle BOTH the per-window CLOSE and SDL_QUIT: SDL only auto-translates the X button
               into SDL_QUIT when the closing window is the LAST SDL window, but the GL host-GPU
               backend (glgpu.c) keeps a hidden second OPENGL window alive -- so for GL titles the X
               produced a CLOSE with no SDL_QUIT and the old SDL_QUIT-only handler ignored it (the
               window wouldn't close; only Esc, which routes through request_exit directly, worked). */
            if (e.type == SDL_QUIT ||
                (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE
                 && e.window.windowID == SDL_GetWindowID(win))) {
#ifdef ME_WINMENU
                int do_quit = request_exit(hwnd);
#else
                int do_quit = 1;
#endif
                /* a single X click can yield a CLOSE *and* an SDL_QUIT; collapse the pair so the
                   confirm dialog runs once (and a stray queued close can't re-prompt on cancel). */
                SDL_FlushEvent(SDL_QUIT);
                SDL_FlushEvent(SDL_WINDOWEVENT);
                if (do_quit) running = 0;
                continue;
            }
            if (e.type == SDL_CONTROLLERDEVICEADDED) {
                if (g_npads < ME_MAXPADS) { SDL_GameController *c = SDL_GameControllerOpen(e.cdevice.which);
                    if (c) g_pads[g_npads++] = c; }
                continue;
            }
            if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                for (int i = 0; i < g_npads; i++)
                    if (g_pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[i])) == e.cdevice.which) {
                        SDL_GameControllerClose(g_pads[i]);
                        g_pads[i] = g_pads[--g_npads]; g_pads[g_npads] = NULL; break;
                    }
                continue;
            }
            /* keybind settings overlay: while open it owns keyboard/gamepad/mouse input (those
               return consumed -> skip the rest). It does NOT consume Win32 menu commands
               (SDL_SYSWMEVENT returns 0), so the menu bar still works -- e.g. File > Open, which
               loads a game and closes the overlay (see start_game). */
            if (su_is_open(&g_su) && su_handle_event(&g_su, &e)) continue;
            /* mouse -> touchscreen. With SDL_RenderSetLogicalSize the EVENT coords are already in
               guest (logical) pixels (unlike SDL_GetMouseState, which is window pixels). */
            if (e.type == SDL_MOUSEMOTION) { touch_x = e.motion.x; touch_y = e.motion.y; }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                touch_down = 1; touch_x = e.button.x; touch_y = e.button.y; }
            else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) touch_down = 0;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode kc = e.key.keysym.sym; Uint16 mod = e.key.keysym.mod;
#ifdef ME_WINMENU
                if (kc == SDLK_F1) kbwin_open(hwnd, &g_ic, (int)shm->device);   /* native keybind editor */
#else
                if (kc == SDLK_F1) su_open(&g_su, (int)shm->device);   /* in-SDL keybind overlay */
#endif
#ifdef ME_WINMENU
                else if (kc == SDLK_ESCAPE) { if (request_exit(hwnd)) running = 0; }
#else
                else if (kc == SDLK_ESCAPE) running = 0;
#endif
                else if (kc == SDLK_F11 || (kc == SDLK_RETURN && (mod & KMOD_ALT)))
                    toggle_fullscreen(win);
                else if (kc == SDLK_F12) save_screenshot(cur_w, cur_h);
                else if (kc == SDLK_F9) input_record_toggle();   /* start/stop input recording */
                /* Savestates. !e.key.repeat matters here in a way it does not for F9/F12: SDL
                   delivers auto-repeat as further KEYDOWNs, and a held F5 would otherwise fire a
                   whole-machine save every few milliseconds. EDIT_OPEN() keeps them from firing
                   while a settings window has the keyboard. */
                else if (!e.key.repeat && !EDIT_OPEN() && kc == SDLK_F5)
                    state_request(ME_STATE_REQ_SAVE,
                                  (mod & KMOD_SHIFT) ? g_cur_slot : ME_STATE_SLOT_QUICK);
                else if (!e.key.repeat && !EDIT_OPEN() && kc == SDLK_F8)
                    state_request(ME_STATE_REQ_LOAD,
                                  (mod & KMOD_SHIFT) ? g_cur_slot : ME_STATE_SLOT_QUICK);
                else if (!e.key.repeat && !EDIT_OPEN() && kc == SDLK_F6) state_cycle_slot(-1);
                else if (!e.key.repeat && !EDIT_OPEN() && kc == SDLK_F7) state_cycle_slot(+1);
                /* F4 opens the slot picker; the ALT guard leaves Alt+F4 alone to close the window. */
                else if (!e.key.repeat && !EDIT_OPEN() && kc == SDLK_F4 && !(mod & KMOD_ALT))
                    state_picker_open();
#ifdef ME_WINMENU
                else if (kc == SDLK_o && (mod & KMOD_CTRL) && hwnd) do_open_dialog(hwnd);
#endif
            }
#ifdef ME_WINMENU
            if (e.type == SDL_SYSWMEVENT && e.syswm.msg) {
                SDL_SysWMmsg *wm = e.syswm.msg;   /* .msg.win = {hwnd, msg(UINT), wParam, lParam} */
                if (wm->subsystem == SDL_SYSWM_WINDOWS && wm->msg.win.msg == WM_COMMAND
                    && HIWORD(wm->msg.win.wParam) == 0 && hwnd)
                    handle_menu_command(win, hwnd, (int)LOWORD(wm->msg.win.wParam));
            }
#endif
        }
        if (shm->quit) running = 0;
#ifdef ME_BUNDLED
        if (g_fault_pending) {     /* the engine caught a host fault + returned to idle */
            g_fault_pending = 0;
            char msg[1200];
            snprintf(msg, sizeof msg,
                "The game crashed and was stopped:\n\n%s\n\nFault address: 0x%llx\n\n"
                "The emulator is still running -- use File > Open to load another game.",
                g_cur_game[0] ? g_cur_game : "(unknown)", (unsigned long long)g_fault_addr);
#ifdef _WIN32
            MessageBoxA(hwnd, msg, "magiceyes -- game crashed", MB_ICONERROR | MB_OK);
#else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "magiceyes -- game crashed", msg, win);
#endif
        }
#endif
        shm->viewer_heartbeat++;   /* tell the producer a viewer is consuming a_read */

        /* host input -> canonical GP2X buttons via the active per-device binding profile
           (keyboard + gamepad; ic_compute_buttons derives the 8-way diagonals). The guest shim
           handles any device-native button reorder/axes, so we always write the GP2X layout. */
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        uint32_t b = EDIT_OPEN() ? 0u
                   : ic_compute_buttons(&g_ic, k, g_pads, g_npads, (int)shm->device);
        /* clamp the live touch (Caanoo; mouse -> guest pixels), then record/replay buttons+touch
           together (frame_seq-keyed; see the module above) and apply. */
        int tx = touch_x, ty = touch_y, td = touch_down;
        if (tx < 0) tx = 0;
        if (cur_w > 0 && tx >= cur_w) tx = cur_w - 1;
        if (ty < 0) ty = 0;
        if (cur_h > 0 && ty >= cur_h) ty = cur_h - 1;
        if (EDIT_OPEN()) {
            shm->buttons = 0;            /* pause game input while editing keybinds */
        } else {
            b = input_step(shm, b, &tx, &ty, &td);
            /* ME_AUTOKEY="keys" -- scripted input for headless control testing. Once the guest has
               actually rendered (frame_seq), press each key (u/d/l/r/a/b/x/y/s=start/e=select) in
               turn, ~0.4s held per 0.8s slot -- decoupled from the variable boot time. */
            { const char *ak = getenv("ME_AUTOKEY");
              if (ak && shm->frame_seq > 60) { static Uint32 _t0 = 0; if (!_t0) _t0 = SDL_GetTicks();
                  const char *ks = strchr(ak, ','); ks = ks ? ks + 1 : ak;
                  double el = (SDL_GetTicks() - _t0) / 1000.0; int idx = (int)(el / 0.8); double in = el - idx * 0.8;
                  if (idx >= 0 && idx < (int)strlen(ks) && in < 0.4) {
                      static const char cs[] = "udlrabxyse";
                      static const int  bs[] = { GP2X_UP, GP2X_DOWN, GP2X_LEFT, GP2X_RIGHT, GP2X_A,
                                                 GP2X_B, GP2X_X, GP2X_Y, GP2X_START, GP2X_SELECT };
                      const char *p = strchr(cs, ks[idx]); if (p) { b |= 1u << bs[p - cs];
                          static int last = -1; if (idx != last) { last = idx;
                              fprintf(stderr, "[autokey] press '%c' (seq=%u)\n", ks[idx], shm->frame_seq); } } } } }
            if (g_rep_on || !getenv("ME_VIEWER_NOINPUT")) {
                shm->buttons = b;
                shm->touch_x = (int16_t)tx; shm->touch_y = (int16_t)ty; shm->touch_down = (uint32_t)td;
            }
        }

        /* audio is serviced on its own thread (see audio_thread) */

        /* window header: system + rendering backend + fps (updated ~2x/sec) */
        { static Uint32 t0 = 0; static uint32_t s0 = 0;
          Uint32 now = SDL_GetTicks();
          if (t0 == 0) { t0 = now; s0 = shm->frame_seq; }
          else if (now - t0 >= 500) {
              double fps = (double)(shm->frame_seq - s0) * 1000.0 / (double)(now - t0);
              static const char *dev[] = { "GP2X", "GP2X Wiz", "GP2X Caanoo" };
              static const char *bk[]  = { "FB", "SDL", "GL" };
              int di = shm->device  < 3 ? shm->device  : 0;
              int bi = shm->backend < 3 ? shm->backend : 0;
              const char *ind = g_rec ? "  |  [REC]" : (g_rep_on && !g_rep_done) ? "  |  [REPLAY]" : "";
              char title[200];
              snprintf(title, sizeof title, "magiceyes  |  %s  |  %s  |  %.0f fps%s",
                       dev[di], bk[bi], fps, ind);
              SDL_SetWindowTitle(win, title);
              t0 = now; s0 = shm->frame_seq;
          }
        }

#ifdef ME_BUNDLED
        if (g_fwlog) { static Uint32 lt = 0; Uint32 nw = SDL_GetTicks();
            if (nw - lt >= 1000) { lt = nw;
                me_log("[fw] viewer: shm %dx%d seq=%u | cur %dx%d last_seq=%u tex=%p present=%d\n",
                       (int)shm->width, (int)shm->height, shm->frame_seq, cur_w, cur_h,
                       last_seq, (void *)tex, (shm->frame_seq != last_seq && cur_w > 0)); } }
#endif
        /* resize texture if the game changed mode */
        if ((int)shm->width != cur_w || (int)shm->height != cur_h) {
            cur_w = (int)shm->width; cur_h = (int)shm->height;
#ifdef ME_WINMENU
            g_shot_w = cur_w; g_shot_h = cur_h;
#endif
            if (cur_w > 0 && cur_h > 0) {
                g_base_w = cur_w; g_base_h = cur_h;   /* scale presets follow the new mode */
                SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
                    SDL_TEXTUREACCESS_STREAMING, cur_w, cur_h);
                SDL_RenderSetLogicalSize(ren, cur_w, cur_h);
#ifdef ME_BUNDLED
                if (g_fwlog) me_log("[fw] viewer: texture (re)created %dx%d tex=%p err='%s'\n",
                                    cur_w, cur_h, (void *)tex, SDL_GetError());
#endif
            }
        }

        if (su_is_open(&g_su)) {
            /* settings overlay owns the screen: re-present every loop (the game may not be
               producing new frames) -- last frame underneath, then the overlay on top. */
            if (cur_w > 0) { SDL_UpdateTexture(tex, NULL, shm->pixels, GP2XSHM_MAXW * 2);
                             SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, NULL, NULL); }
            else SDL_RenderClear(ren);
            su_render(&g_su, ren, cur_w > 0 ? cur_w : g_base_w, cur_h > 0 ? cur_h : g_base_h);
            toast_render(ren, cur_w > 0 ? cur_w : g_base_w, cur_h > 0 ? cur_h : g_base_h);
            SDL_RenderPresent(ren);
            last_seq = shm->frame_seq;
            SDL_Delay(16);
        } else if (shm->frame_seq != last_seq && cur_w > 0) {
            last_seq = shm->frame_seq;
            /* shm rows are GP2XSHM_MAXW wide; upload only cur_w x cur_h */
            int e1 = SDL_UpdateTexture(tex, NULL, shm->pixels, GP2XSHM_MAXW * 2);
            int e2 = SDL_RenderClear(ren);
            int e3 = SDL_RenderCopy(ren, tex, NULL, NULL);
            toast_render(ren, cur_w, cur_h);
            SDL_RenderPresent(ren);
            /* ME_AUTOSHOT=N: save the actual shm framebuffer to screenshots/ after N seconds, once.
               Captures the engine's real output regardless of window occlusion (a screen-grab of a
               backgrounded window catches whatever is on top instead). */
            { const char *as = getenv("ME_AUTOSHOT");
              if (as) { static Uint32 t0 = 0; static int done = 0; Uint32 nw = SDL_GetTicks();
                  if (!t0) t0 = nw;
                  if (!done && nw - t0 >= (Uint32)(atoi(as) * 1000)) { save_screenshot(cur_w, cur_h); done = 1; } } }
#ifdef ME_BUNDLED
            if (g_fwlog) { static Uint32 lp = 0; Uint32 nw = SDL_GetTicks();
                if (nw - lp >= 1000) { lp = nw;   /* sample the centre row of the shm: is there content? */
                    const uint16_t *px = (const uint16_t *)shm->pixels + (size_t)(cur_h / 2) * GP2XSHM_MAXW;
                    int nz = 0; for (int x = 0; x < cur_w; x++) if (px[x]) nz++;
                    me_log("[fw] present: UpdateTexture=%d RenderClear=%d RenderCopy=%d shm_nz_row=%d/%d err='%s'\n",
                           e1, e2, e3, nz, cur_w, SDL_GetError()); } }
#else
            (void)e1; (void)e2; (void)e3;
#endif
        } else if (shm->frame_seq == 0) {
            /* no game has presented yet -> paint black so the window doesn't show its
               uninitialised backbuffer (the white strip); stop once a game renders. */
            SDL_RenderClear(ren);
            toast_render(ren, g_base_w, g_base_h);
            SDL_RenderPresent(ren);
            SDL_Delay(16);
        } else if (toast_live()) {
            /* A toast has to keep animating even when the game has stopped producing frames --
               which is precisely the moment after a load is requested. Re-present the last frame
               with the message over it rather than sitting in the idle branch. */
            if (cur_w > 0) { SDL_UpdateTexture(tex, NULL, shm->pixels, GP2XSHM_MAXW * 2);
                             SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, NULL, NULL); }
            else SDL_RenderClear(ren);
            toast_render(ren, cur_w > 0 ? cur_w : g_base_w, cur_h > 0 ? cur_h : g_base_h);
            SDL_RenderPresent(ren);
            SDL_Delay(16);
        } else {
            SDL_Delay(5);
        }
    }
    shm->quit = 1;                     /* signal the audio thread to exit */
    if (ath) SDL_WaitThread(ath, NULL);
    SDL_Quit();
    return 0;
}

#ifndef ME_BUNDLED
int main(int argc, char **argv) {
    int scale = 3, fullscreen = 0, mute = 0, volume = 100;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (!strcmp(a, "-s") || !strcmp(a, "--scale"))      { if (++i < argc) scale = atoi(argv[i]); }
        else if (!strcmp(a, "-f") || !strcmp(a, "--fullscreen")) fullscreen = 1;
        else if (!strcmp(a, "--mute"))                           mute = 1;
        else if (!strcmp(a, "--volume"))                         { if (++i < argc) volume = atoi(argv[i]); }
        else if (a[0] != '-')                                    scale = atoi(a);   /* legacy positional scale */
    }
    int fd = shm_open(GP2XSHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { perror("shm_open"); return 1; }
    if (ftruncate(fd, sizeof(gp2x_shm_t)) != 0) { /* may pre-exist */ }
    gp2x_shm_t *m = mmap(NULL, sizeof(gp2x_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { perror("mmap"); return 1; }
    return viewer_run(m, scale, fullscreen, mute, volume);
}
#endif
