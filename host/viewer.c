/* Native x86 SDL2 viewer for the GP2X shim.
 * Maps /dev/shm/gp2x_fb, shows the RGB565 framebuffer in a scaled window
 * (WSLg/X11), feeds keyboard input back as GP2X buttons, and plays the PCM
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
static unsigned long long g_consumed = 0;
static int g_underruns = 0, g_cb_calls = 0;

static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    uint32_t w = shm->a_write, r = shm->a_read;
    uint32_t avail = w - r;                 /* unsigned wrap-safe */
    int n = (int)avail; if (n > len) n = len;
    for (int i = 0; i < n; i++) stream[i] = shm->aring[(r + i) % GP2XSHM_ARING];
    if (n < len) { memset(stream + n, 0, len - n); g_underruns++; }
    shm->a_read = r + n;
    g_consumed += n; g_cb_calls++;
}

int main(int argc, char **argv) {
    int scale = (argc > 1) ? atoi(argv[1]) : 3;
    if (scale < 1) scale = 1;

    int fd = shm_open(GP2XSHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { perror("shm_open"); return 1; }
    if (ftruncate(fd, sizeof(gp2x_shm_t)) != 0) { /* may pre-exist */ }
    shm = mmap(NULL, sizeof(gp2x_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); return 1; }

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

    int audio_open = 0, audio_started = 0;
    uint32_t last_seq = ~0u;
    int running = 1;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
        }
        if (shm->quit) running = 0;

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
        shm->buttons = b;

        /* late audio open once the shim advertises a format */
        if (!audio_open && shm->audio_active && shm->audio_freq) {
            SDL_AudioSpec want;
            memset(&want, 0, sizeof(want));
            want.freq = (int)shm->audio_freq;
            want.format = (Uint16)shm->audio_format;
            want.channels = (Uint8)shm->audio_channels;
            want.samples = 1024;
            want.callback = audio_cb;
            /* obtained=NULL -> SDL2 converts hardware format to exactly `want`,
               so our callback always receives ring-matching PCM. Opens PAUSED. */
            if (SDL_OpenAudio(&want, NULL) == 0) { audio_open = 1;
                fprintf(stderr, "viewer: audio %dHz fmt=%04x ch=%d (prebuffering)\n",
                        want.freq, want.format, want.channels); }
            else fprintf(stderr, "viewer: SDL_OpenAudio failed: %s\n", SDL_GetError());
        }
        /* start playback only once the producer has built up a cushion */
        if (audio_open && !audio_started &&
            (shm->a_write - shm->a_read) >= 10000) {
            SDL_PauseAudio(0); audio_started = 1;
            fprintf(stderr, "viewer: playback started (ring=%u)\n",
                    shm->a_write - shm->a_read);
        }

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

        { static Uint32 t0 = 0, tp = 0;
          Uint32 now = SDL_GetTicks(); if (!t0) t0 = now;
          if (now - tp >= 2000) { tp = now;
            double secs = (now - t0) / 1000.0;
            fprintf(stderr, "viewer audio: %.0f B/s (want %d), underruns=%d/%d, ring=%u\n",
                    g_consumed / (secs > 0 ? secs : 1), shm->audio_freq * shm->audio_channels * 2,
                    g_underruns, g_cb_calls, shm->a_write - shm->a_read); } }

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
    if (audio_open) SDL_CloseAudio();
    SDL_Quit();
    return 0;
}
