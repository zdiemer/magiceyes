/* settings_ui -- the in-app keybind settings overlay drawn over the framebuffer by the
 * SDL2 viewer. Cross-platform (no Win32, no SDL_ttf -- uses the embedded font8x8). Opened
 * by a hotkey (F1) and, on the Windows bundle, a menu item. Lets the user pick a device
 * profile (GP2X / Wiz / Caanoo), rebind each GP2X button by pressing a key or gamepad
 * button, clear/reset bindings, and saves to bindings.conf on close. */
#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include <SDL2/SDL.h>
#include "input_config.h"

typedef struct {
    int        open;        /* overlay visible + capturing all input */
    int        device;      /* current profile tab 0..IC_NDEV-1 */
    int        row;         /* selected bindable-button row (index into ic_bindable[]) */
    int        capturing;   /* 1 = waiting for the user to press an input to bind */
    ic_config *cfg;         /* not owned */
} su_state;

void su_init(su_state *s, ic_config *cfg);
void su_open(su_state *s, int device);
void su_close_and_save(su_state *s);     /* persists via ic_save */
int  su_is_open(const su_state *s);

/* Feed every SDL event here while open; returns 1 if the overlay consumed it (the caller
   must then NOT treat it as game input or a window hotkey). */
int  su_handle_event(su_state *s, const SDL_Event *e);

/* Draw the overlay; view_w/view_h are the current logical (guest-pixel) dimensions. */
void su_render(su_state *s, SDL_Renderer *ren, int view_w, int view_h);

#endif /* SETTINGS_UI_H */
