/* settings_ui -- the in-app keybind settings overlay drawn over the framebuffer by the
 * SDL2 viewer. Cross-platform (no Win32, no SDL_ttf -- uses the embedded font8x8). Opened
 * by a hotkey (F1) and, on the Windows bundle, a menu item. Lets the user pick a device
 * profile (GP2X / Wiz / Caanoo), rebind each GP2X button by pressing a key or gamepad
 * button, clear/reset bindings, and saves to bindings.conf on close. */
#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include <SDL2/SDL.h>
#include "input_config.h"

/* The overlay has two pages. Input is the original keybind table; States is the savestate slot
   picker for builds with no native toolkit (the Windows bundle uses state_win.c instead). Tab
   switches between them. */
enum { SU_PAGE_INPUT = 0, SU_PAGE_STATES };

typedef struct {
    int        open;        /* overlay visible + capturing all input */
    int        device;      /* current profile tab 0..IC_NDEV-1 */
    int        row;         /* selected bindable-button row (index into ic_bindable[]) */
    int        capturing;   /* 1 = waiting for the user to press an input to bind */
    ic_config *cfg;         /* not owned */
    int        page;        /* SU_PAGE_* */
    int        srow;        /* selected slot on the States page (0 = quick, 1..N) */
    /* Supplied by the viewer so the page never has to know whether a request goes straight to
       the engine or through the shm bytes -- exactly what the Win32 picker does. */
    void     (*on_save)(int slot);
    void     (*on_load)(int slot);
} su_state;

void su_init(su_state *s, ic_config *cfg);
void su_open(su_state *s, int device);
void su_open_page(su_state *s, int device, int page);
void su_set_state_hooks(su_state *s, void (*on_save)(int slot), void (*on_load)(int slot));
void su_close_and_save(su_state *s);     /* persists via ic_save */
int  su_is_open(const su_state *s);

/* Feed every SDL event here while open; returns 1 if the overlay consumed it (the caller
   must then NOT treat it as game input or a window hotkey). */
int  su_handle_event(su_state *s, const SDL_Event *e);

/* Draw one line of text with the embedded font8x8, `px` device pixels per font pixel. Exported
   for the viewer's on-screen toast: same font, same renderer, no second text path. */
void su_draw_text(SDL_Renderer *ren, int x, int y, int px, SDL_Color c, const char *s);

/* Draw the overlay; view_w/view_h are the current logical (guest-pixel) dimensions. */
void su_render(su_state *s, SDL_Renderer *ren, int view_w, int view_h);

#endif /* SETTINGS_UI_H */
