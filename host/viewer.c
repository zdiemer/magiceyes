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

#if defined(_WIN32) && defined(ME_BUNDLED)
/* Native Win32 menu bar (File/View/Audio/Help) on the SDL window. The window proc + SDL's
   event pump run on this (the viewer) thread, so the menu, WM_COMMAND, and the file dialog
   all live here -- no thread hop. File->Open hot-reloads via the engine (same process). */
#include <SDL2/SDL_syswm.h>
#include <windows.h>
#include <commdlg.h>
#include <direct.h>   /* _mkdir for the recent-files dir */
/* engine entry points (plain C signatures -- avoid including engine.h / unicorn here) */
const char *resolve_input(const char *in, char *out, size_t cap);
int  classify_elf(const char *path);
void engine_request_reload(const char *host_path);
#define ME_WINMENU 1
#endif

static gp2x_shm_t *shm;
static unsigned long long g_consumed = 0;   /* real audio bytes played (B/s stat) */
static unsigned long long g_fed = 0;        /* all bytes queued incl. silence pad */
static volatile int g_view_mute = 0;        /* Audio->Mute / --mute */
static volatile int g_view_volume = 100;    /* Audio->Volume / --volume (0..100) */

/* Queue PCM applying mute/volume. At 100% and unmuted, queue straight from the ring (fast
   path). Otherwise scale into a scratch buffer: S16 samples scale linearly; U8 around 128. */
static void queue_scaled(SDL_AudioDeviceID dev, const uint8_t *src, uint32_t n) {
    int vol = g_view_mute ? 0 : g_view_volume;
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
            if (queued < target) {
                uint32_t n = target - queued; if (n > avail) n = avail; n -= n % frame;
                if (n) {
                    uint32_t r = shm->a_read % GP2XSHM_ARING;
                    uint32_t first = GP2XSHM_ARING - r; if (first > n) first = n;
                    queue_scaled(adev, shm->aring + r, first);
                    if (n > first) queue_scaled(adev, shm->aring, n - first);
                    shm->a_read += n; g_consumed += n; g_fed += n;
                }
                /* pad with silence on a producer gap so the device never underruns */
                uint32_t still = target - SDL_GetQueuedAudioSize(adev); still -= still % frame;
                if (still) {
                    static uint8_t zeros[8192];
                    for (uint32_t z = still; z; ) { uint32_t c = z > sizeof(zeros) ? sizeof(zeros) : z;
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

#ifdef ME_WINMENU
/* ---- Win32 menu bar (bundle only: File->Open hot-reloads via the in-process engine) ---- */
enum { IDM_OPEN = 1001, IDM_RELOAD, IDM_EXIT,
       IDM_SCALE1 = 1010, IDM_SCALE2, IDM_SCALE3, IDM_SCALE4, IDM_FULLSCREEN,
       IDM_MUTE = 1020, IDM_VOL25, IDM_VOL50, IDM_VOL75, IDM_VOL100,
       IDM_ABOUT = 1030, IDM_CONTROLS,
       IDM_RECENT0 = 1100 };
#define MAX_RECENT 8
static HMENU g_recentmenu;
static char g_recent[MAX_RECENT][MAX_PATH]; static int g_nrecent;
static char g_last_game[MAX_PATH];

static void recent_path(char *out, size_t cap) {
    const char *ad = getenv("APPDATA"); if (!ad) ad = getenv("TEMP"); if (!ad) ad = ".";
    char dir[MAX_PATH]; snprintf(dir, sizeof dir, "%s\\magiceyes", ad); _mkdir(dir);
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
static void build_menu(HWND hwnd) {
    HMENU bar = CreateMenu(), file = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, IDM_OPEN, "&Open...\tCtrl+O");
    g_recentmenu = CreatePopupMenu();
    AppendMenuA(file, MF_POPUP, (UINT_PTR)g_recentmenu, "Open &Recent");
    AppendMenuA(file, MF_STRING, IDM_RELOAD, "&Reload");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, IDM_EXIT, "E&xit");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&File");
    HMENU view = CreatePopupMenu();
    AppendMenuA(view, MF_STRING, IDM_SCALE1, "Scale &1x");
    AppendMenuA(view, MF_STRING, IDM_SCALE2, "Scale &2x");
    AppendMenuA(view, MF_STRING, IDM_SCALE3, "Scale &3x");
    AppendMenuA(view, MF_STRING, IDM_SCALE4, "Scale &4x");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, IDM_FULLSCREEN, "&Fullscreen\tF11");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)view, "&View");
    HMENU audio = CreatePopupMenu();
    AppendMenuA(audio, MF_STRING, IDM_MUTE, "&Mute");
    AppendMenuA(audio, MF_SEPARATOR, 0, NULL);
    AppendMenuA(audio, MF_STRING, IDM_VOL25, "Volume 25%");
    AppendMenuA(audio, MF_STRING, IDM_VOL50, "Volume 50%");
    AppendMenuA(audio, MF_STRING, IDM_VOL75, "Volume 75%");
    AppendMenuA(audio, MF_STRING, IDM_VOL100, "Volume 100%");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)audio, "&Audio");
    HMENU help = CreatePopupMenu();
    AppendMenuA(help, MF_STRING, IDM_CONTROLS, "&Controls");
    AppendMenuA(help, MF_STRING, IDM_ABOUT, "&About");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)help, "&Help");
    recent_load(); recent_rebuild_menu();
    SetMenu(hwnd, bar);
    if (g_view_mute) CheckMenuItem(bar, IDM_MUTE, MF_BYCOMMAND | MF_CHECKED);
}
static void start_game(const char *path) {
    char bin[MAX_PATH * 2];
    const char *r = resolve_input(path, bin, sizeof bin);
    if (!r) { MessageBoxA(NULL, "No .gpe found, or the folder/zip is ambiguous.\nSee the console for details.",
                          "magiceyes", MB_ICONERROR); return; }
    int c = classify_elf(r);
    if (c == 1) { MessageBoxA(NULL, "That title is dynamically linked -- the native build can't run it yet "
                             "(GP2X static + GPEComp only).", "magiceyes", MB_ICONWARNING); return; }
    if (c < 0) { MessageBoxA(NULL, "Not a usable GP2X ARM binary.\nSee the console for details.",
                            "magiceyes", MB_ICONERROR); return; }
    snprintf(g_last_game, sizeof g_last_game, "%s", path);
    recent_add(path); recent_rebuild_menu();
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
static void handle_menu_command(SDL_Window *win, HWND hwnd, int id) {
    switch (id) {
    case IDM_OPEN:   do_open_dialog(hwnd); break;
    case IDM_RELOAD: if (g_last_game[0]) start_game(g_last_game); break;
    case IDM_EXIT:   { SDL_Event q; memset(&q, 0, sizeof q); q.type = SDL_QUIT; SDL_PushEvent(&q); } break;
    case IDM_SCALE1: apply_scale(win, 1); break;
    case IDM_SCALE2: apply_scale(win, 2); break;
    case IDM_SCALE3: apply_scale(win, 3); break;
    case IDM_SCALE4: apply_scale(win, 4); break;
    case IDM_FULLSCREEN: toggle_fullscreen(win); break;
    case IDM_MUTE:   g_view_mute = !g_view_mute;
        CheckMenuItem(GetMenu(hwnd), IDM_MUTE, MF_BYCOMMAND | (g_view_mute ? MF_CHECKED : MF_UNCHECKED)); break;
    case IDM_VOL25:  g_view_volume = 25;  g_view_mute = 0; break;
    case IDM_VOL50:  g_view_volume = 50;  g_view_mute = 0; break;
    case IDM_VOL75:  g_view_volume = 75;  g_view_mute = 0; break;
    case IDM_VOL100: g_view_volume = 100; g_view_mute = 0; break;
    case IDM_CONTROLS:
        MessageBoxA(hwnd, "D-pad:    Arrow keys\nA/B/X/Y:  Z / X / A / S\nStart:    Enter\n"
                          "Select:   Backspace\nL / R:    Q / W\nFullscreen: F11\nQuit:     Esc",
                    "Controls", MB_OK); break;
    case IDM_ABOUT:
        MessageBoxA(hwnd, "magiceyes\nRun GP2X / Wiz games on a PC.\ngithub.com/zdiemer/magiceyes",
                    "About magiceyes", MB_OK); break;
    default:
        if (id >= IDM_RECENT0 && id < IDM_RECENT0 + MAX_RECENT) {
            int i = id - IDM_RECENT0; if (i < g_nrecent) start_game(g_recent[i]);
        }
    }
}
#endif /* ME_WINMENU */

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

    /* When PULSE_SERVER is set the box is running PulseAudio (possibly over a
       socket, e.g. a remote/containerised display); SDL may otherwise default to
       a backend (ALSA) with no usable device and fail to open. Prefer PulseAudio
       in that case, unless the user pinned SDL_AUDIODRIVER. Harmless on a normal
       desktop, where PULSE_SERVER is usually unset and SDL autodetects. */
    if (getenv("PULSE_SERVER") && !getenv("SDL_AUDIODRIVER")) {
        setenv("SDL_AUDIODRIVER", "pulseaudio", 1);
    }
    /* Request a generous PulseAudio server buffer; some setups otherwise drop the
       stream under latency spikes. */
    if (!getenv("PULSE_LATENCY_MSEC")) setenv("PULSE_LATENCY_MSEC", "120", 1);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    int w = shm->width ? (int)shm->width : 320;
    int h = shm->height ? (int)shm->height : 240;
    g_base_w = w; g_base_h = h;
    Uint32 wflags = SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_Window *win = SDL_CreateWindow("magiceyes",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w * scale, h * scale, wflags);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);   /* black clear/letterbox (avoid an unpainted strip) */
    SDL_RenderSetLogicalSize(ren, w, h);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, w, h);
    int cur_w = w, cur_h = h;

#ifdef ME_WINMENU
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); HWND hwnd = NULL;
    if (SDL_GetWindowWMInfo(win, &wmi)) hwnd = wmi.info.win.window;
    if (hwnd) {
        build_menu(hwnd);
        if (!fullscreen) SDL_SetWindowSize(win, w * scale, h * scale);  /* regrow past the menu bar */
    }
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);   /* deliver WM_COMMAND to SDL_PollEvent */
#endif

    SDL_Thread *ath = SDL_CreateThread(audio_thread, "gp2x-audio", NULL);
    uint32_t last_seq = ~0u;
    int running = 1;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode kc = e.key.keysym.sym; Uint16 mod = e.key.keysym.mod;
                if (kc == SDLK_ESCAPE) running = 0;
                else if (kc == SDLK_F11 || (kc == SDLK_RETURN && (mod & KMOD_ALT)))
                    toggle_fullscreen(win);
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
        shm->viewer_heartbeat++;   /* tell the producer a viewer is consuming a_read */

        /* keyboard -> GP2X buttons */
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        uint32_t b = 0;
        int up = k[SDL_SCANCODE_UP], dn = k[SDL_SCANCODE_DOWN];
        int lf = k[SDL_SCANCODE_LEFT], rt = k[SDL_SCANCODE_RIGHT];
        if (up) b |= 1u << GP2X_UP;
        if (dn) b |= 1u << GP2X_DOWN;
        if (lf) b |= 1u << GP2X_LEFT;
        if (rt) b |= 1u << GP2X_RIGHT;
        if (up && lf) b |= 1u << GP2X_UPLEFT;
        if (up && rt) b |= 1u << GP2X_UPRIGHT;
        if (dn && lf) b |= 1u << GP2X_DOWNLEFT;
        if (dn && rt) b |= 1u << GP2X_DOWNRIGHT;
        if (k[SDL_SCANCODE_Z]) b |= 1u << GP2X_A;
        if (k[SDL_SCANCODE_X]) b |= 1u << GP2X_B;
        if (k[SDL_SCANCODE_A]) b |= 1u << GP2X_X;
        if (k[SDL_SCANCODE_S]) b |= 1u << GP2X_Y;
        if (k[SDL_SCANCODE_RETURN]) b |= 1u << GP2X_START;
        if (k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_BACKSPACE]) b |= 1u << GP2X_SELECT;
        if (k[SDL_SCANCODE_Q]) b |= 1u << GP2X_L;
        if (k[SDL_SCANCODE_W]) b |= 1u << GP2X_R;
        if (!getenv("ME_VIEWER_NOINPUT")) shm->buttons = b;  /* allow scripted input */

        /* audio is serviced on its own thread (see audio_thread) */

        /* resize texture if the game changed mode */
        if ((int)shm->width != cur_w || (int)shm->height != cur_h) {
            cur_w = (int)shm->width; cur_h = (int)shm->height;
            if (cur_w > 0 && cur_h > 0) {
                g_base_w = cur_w; g_base_h = cur_h;   /* scale presets follow the new mode */
                SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
                    SDL_TEXTUREACCESS_STREAMING, cur_w, cur_h);
                SDL_RenderSetLogicalSize(ren, cur_w, cur_h);
            }
        }

        if (shm->frame_seq != last_seq && cur_w > 0) {
            last_seq = shm->frame_seq;
            /* shm rows are GP2XSHM_MAXW wide; upload only cur_w x cur_h */
            SDL_UpdateTexture(tex, NULL, shm->pixels, GP2XSHM_MAXW * 2);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
        } else if (shm->frame_seq == 0) {
            /* no game has presented yet -> paint black so the window doesn't show its
               uninitialised backbuffer (the white strip); stop once a game renders. */
            SDL_RenderClear(ren);
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
