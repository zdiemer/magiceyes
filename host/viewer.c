/* Native x86 SDL2 viewer for the GP2X shim.
 * Maps /dev/shm/gp2x_fb, shows the RGB565 framebuffer in a scaled window
 * (X11/Wayland), feeds keyboard input back as GP2X buttons, and plays the PCM
 * ring the shim produces. Build with host gcc + SDL2 (NOT the ARM toolchain).
 */
#include <SDL2/SDL.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "gp2xshm.h"

static gp2x_shm_t *shm;
static unsigned long long g_consumed = 0;   /* real audio bytes played (B/s stat) */
static unsigned long long g_fed = 0;        /* all bytes queued incl. silence pad */

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
                    SDL_QueueAudio(adev, shm->aring + r, first);
                    if (n > first) SDL_QueueAudio(adev, shm->aring, n - first);
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

int main(int argc, char **argv) {
    int scale = (argc > 1) ? atoi(argv[1]) : 3;
    if (scale < 1) scale = 1;

    int fd = shm_open(GP2XSHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { perror("shm_open"); return 1; }
    if (ftruncate(fd, sizeof(gp2x_shm_t)) != 0) { /* may pre-exist */ }
    shm = mmap(NULL, sizeof(gp2x_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); return 1; }

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
    SDL_Window *win = SDL_CreateWindow("GP2X/Wiz (romnas)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w * scale, h * scale, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, w, h);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, w, h);
    int cur_w = w, cur_h = h;

    SDL_Thread *ath = SDL_CreateThread(audio_thread, "gp2x-audio", NULL);
    uint32_t last_seq = ~0u;
    int running = 1;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
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
        } else {
            SDL_Delay(5);
        }
    }
    shm->quit = 1;                     /* signal the audio thread to exit */
    if (ath) SDL_WaitThread(ath, NULL);
    SDL_Quit();
    return 0;
}
