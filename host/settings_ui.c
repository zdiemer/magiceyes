/* settings_ui -- see settings_ui.h. Renders with the embedded font8x8 (no SDL_ttf). */
#include "settings_ui.h"
#include "font8x8.h"
#include "state_file.h"   /* the States page reads slots through the container, not the engine */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- text drawing (font8x8: bit 0 = leftmost pixel) ----
   Exported rather than static so the viewer's on-screen toast can draw with the same font and
   the same renderer state; there is no second text path in this program. */
void su_draw_text(SDL_Renderer *ren, int x, int y, int px, SDL_Color c, const char *s) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s; if (ch >= 128) ch = '?';
        const uint8_t *g = font8x8_basic[ch];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8 && bits; col++)
                if (bits & (1u << col)) {
                    SDL_Rect r = { x + col * px, y + row * px, px, px };
                    SDL_RenderFillRect(ren, &r);
                }
        }
        x += 8 * px;
    }
}

static ic_binding *cur_binding(su_state *s) {
    return &s->cfg->prof[s->device].btn[ic_bindable[s->row]];
}

/* On capture: drop existing sources of the SAME category (key/pad-button/axis), then add the
   new one -- so rebinding the key for a button keeps its gamepad binding, and vice versa. */
static void bind_capture(ic_binding *b, const ic_source *ns) {
    int n = 0;
    for (int i = 0; i < b->nsrc; i++)
        if (b->src[i].type != ns->type) b->src[n++] = b->src[i];
    b->nsrc = n;
    if (b->nsrc < IC_MAX_SRC) b->src[b->nsrc++] = *ns;
}

void su_init(su_state *s, ic_config *cfg) {
    memset(s, 0, sizeof *s);
    s->cfg = cfg;
}
void su_open(su_state *s, int device) { su_open_page(s, device, SU_PAGE_INPUT); }

void su_open_page(su_state *s, int device, int page) {
    s->open = 1; s->capturing = 0; s->row = 0;
    s->page = (page == SU_PAGE_STATES) ? SU_PAGE_STATES : SU_PAGE_INPUT;
    s->device = (device >= 0 && device < IC_NDEV) ? device : 0;

}

void su_set_state_hooks(su_state *s, void (*on_save)(int), void (*on_load)(int)) {
    s->on_save = on_save; s->on_load = on_load;
}
void su_close_and_save(su_state *s) {
    s->open = 0; s->capturing = 0;
    ic_save(s->cfg);
}
int su_is_open(const su_state *s) { return s->open; }


/* ---- the States page --------------------------------------------------------------
 * The savestate slot picker for builds with no native toolkit, which in practice means the
 * standalone two-process viewer. The Windows bundle uses state_win.c instead: a real list view
 * with each slot's timestamp and a preview of the saved screen.
 *
 * This page deliberately shows NO metadata and no previews, because it cannot honestly get them.
 * Reading a slot means knowing which directory holds it, and that is <exe_dir>/states/<gamekey>/
 * -- a path derived from the running title, which only the engine knows. In the two-process
 * build this viewer has no engine linked and no way to be told (the shm ABI is shared with the
 * ARM guest shim, so there is nowhere to put a path without moving pixels[]). So it does the one
 * thing it can do correctly: pick a slot and post the request. The engine reports the outcome in
 * the on-screen toast.
 */
#define SU_NSLOTS (ME_STATE_NSLOTS + 1)

void su_close_and_save(su_state *s);   /* defined below; the page closes itself before a load */

/* Returns 1 if the page consumed the event. */
static int su_states_event(su_state *s, const SDL_Event *e) {
    if (e->type != SDL_KEYDOWN) return 0;
    SDL_Keycode k = e->key.keysym.sym;
    if (k == SDLK_UP)   { s->srow = (s->srow + SU_NSLOTS - 1) % SU_NSLOTS; return 1; }
    if (k == SDLK_DOWN) { s->srow = (s->srow + 1) % SU_NSLOTS; return 1; }
    if (k == SDLK_s) { if (s->on_save) s->on_save(s->srow); return 1; }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        if (s->on_load) {
            void (*load)(int) = s->on_load;
            int slot = s->srow;
            su_close_and_save(s);      /* a load tears the guest down; get out of the way first */
            load(slot);
        }
        return 1;
    }
    return 0;
}

static void su_states_render(su_state *s, SDL_Renderer *ren, int view_w, int view_h) {
    const SDL_Color white = { 235, 235, 235, 255 }, dim = { 150, 150, 150, 255 },
                    hi = { 255, 220, 120, 255 };
    int px = view_w >= 480 ? 2 : 1;
    int lh = 10 * px, x = 8 * px, y = 8 * px;

    su_draw_text(ren, x, y, px, hi, "SAVESTATES");
    y += lh * 2;
    for (int i = 0; i < SU_NSLOTS; i++) {
        int sel = (i == s->srow);
        if (sel) {
            SDL_SetRenderDrawColor(ren, 40, 60, 90, 255);
            SDL_Rect r = { x - 2 * px, y - px, 18 * 8 * px, lh };
            SDL_RenderFillRect(ren, &r);
        }
        char line[48];
        snprintf(line, sizeof line, "slot %s", i == 0 ? "quick" : me_state_slot_name(i));
        su_draw_text(ren, x, y, px, sel ? hi : white, line);
        y += lh;
    }
    y += lh;
    su_draw_text(ren, x, y, px, dim, "previews are in the single-process build");
    su_draw_text(ren, x, view_h - lh - 4 * px, px, dim,
                 "Up/Down pick   Enter load   S save   Tab input   Esc close");
}
int su_handle_event(su_state *s, const SDL_Event *e) {
    if (!s->open) return 0;

    /* Tab switches pages; the States page gets first refusal on everything else while it is up. */
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_TAB && !s->capturing) {
        s->page = (s->page == SU_PAGE_STATES) ? SU_PAGE_INPUT : SU_PAGE_STATES;
        return 1;
    }
    if (s->page == SU_PAGE_STATES) {
        if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE) { su_close_and_save(s); return 1; }
        if (su_states_event(s, e)) return 1;
        return e->type == SDL_KEYDOWN;   /* swallow the rest so it is not treated as game input */
    }

    if (s->capturing) {
        if (e->type == SDL_KEYDOWN) {
            if (e->key.keysym.sym == SDLK_ESCAPE) { s->capturing = 0; return 1; }  /* cancel */
            ic_source ns = { IC_SRC_KEY, (int16_t)e->key.keysym.scancode, 0 };
            bind_capture(cur_binding(s), &ns); s->capturing = 0; return 1;
        }
        if (e->type == SDL_CONTROLLERBUTTONDOWN) {
            ic_source ns = { IC_SRC_CBTN, (int16_t)e->cbutton.button, 0 };
            bind_capture(cur_binding(s), &ns); s->capturing = 0; return 1;
        }
        if (e->type == SDL_CONTROLLERAXISMOTION) {
            if (abs(e->caxis.value) > IC_AXIS_THRESH) {
                ic_source ns = { IC_SRC_AXIS, (int16_t)e->caxis.axis, e->caxis.value > 0 ? (int8_t)+1 : (int8_t)-1 };
                bind_capture(cur_binding(s), &ns); s->capturing = 0;
            }
            return 1;   /* consume sub-threshold drift too */
        }
        return 1;       /* swallow all other input while waiting */
    }

    if (e->type == SDL_KEYDOWN) {
        switch (e->key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_F1:       su_close_and_save(s); return 1;
        case SDLK_UP:       s->row = (s->row + ic_nbindable - 1) % ic_nbindable; return 1;
        case SDLK_DOWN:     s->row = (s->row + 1) % ic_nbindable; return 1;
        case SDLK_LEFT:     s->device = (s->device + IC_NDEV - 1) % IC_NDEV; return 1;
        case SDLK_RIGHT:
        case SDLK_TAB:      s->device = (s->device + 1) % IC_NDEV; return 1;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: s->capturing = 1; return 1;
        case SDLK_BACKSPACE:
        case SDLK_DELETE:   cur_binding(s)->nsrc = 0; return 1;
        case SDLK_r:        ic_reset_device(s->cfg, s->device); return 1;
        default:            return 1;   /* swallow every other key while open */
        }
    }
    /* swallow input events so neither the game nor the window hotkeys see them */
    if (e->type == SDL_KEYUP || e->type == SDL_CONTROLLERBUTTONDOWN ||
        e->type == SDL_CONTROLLERBUTTONUP || e->type == SDL_CONTROLLERAXISMOTION ||
        e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP || e->type == SDL_MOUSEMOTION)
        return 1;
    return 0;
}

/* ---- render ---- */
static const char *dev_label[IC_NDEV] = { "GP2X", "WIZ", "CAANOO" };

void su_render(su_state *s, SDL_Renderer *ren, int view_w, int view_h) {
    if (s->page == SU_PAGE_STATES) { su_states_render(s, ren, view_w, view_h); return; }
    if (!s->open || view_w <= 0 || view_h <= 0) return;
    SDL_Color white = { 235, 235, 235, 255 };
    SDL_Color gray  = { 150, 150, 150, 255 };
    SDL_Color yellow= { 245, 220,  80, 255 };
    SDL_Color cyan  = { 120, 210, 235, 255 };

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 205);
    SDL_Rect full = { 0, 0, view_w, view_h };
    SDL_RenderFillRect(ren, &full);

    int x0 = 8, y = 4;
    su_draw_text(ren, x0, y, 1, cyan, "INPUT SETTINGS"); y += 12;

    /* device tabs */
    int tx = x0;
    for (int d = 0; d < IC_NDEV; d++) {
        int wpx = (int)strlen(dev_label[d]) * 8;
        if (d == s->device) {
            SDL_SetRenderDrawColor(ren, 40, 80, 140, 255);
            SDL_Rect hl = { tx - 2, y - 1, wpx + 4, 10 };
            SDL_RenderFillRect(ren, &hl);
        }
        su_draw_text(ren, tx, y, 1, d == s->device ? white : gray, dev_label[d]);
        tx += wpx + 16;
    }
    y += 14;

    /* rows */
    int rowh = 11, labelx = x0, descx = x0 + 8 * 9;
    for (int i = 0; i < ic_nbindable; i++) {
        int bit = ic_bindable[i];
        int ry = y + i * rowh;
        if (i == s->row) {
            SDL_SetRenderDrawColor(ren, 35, 35, 60, 255);
            SDL_Rect hl = { x0 - 2, ry - 1, view_w - 2 * (x0 - 2), 10 };
            SDL_RenderFillRect(ren, &hl);
        }
        SDL_Color lc = (i == s->row) ? white : gray;
        su_draw_text(ren, labelx, ry, 1, lc, ic_button_name(bit));
        if (s->capturing && i == s->row) {
            su_draw_text(ren, descx, ry, 1, yellow, "press a key or gamepad button (Esc=cancel)");
        } else {
            char desc[160];
            ic_binding_describe(&s->cfg->prof[s->device].btn[bit], desc, sizeof desc);
            su_draw_text(ren, descx, ry, 1, lc, desc);
        }
    }

    int fy = y + ic_nbindable * rowh + 4;
    if (fy > view_h - 10) fy = view_h - 10;
    su_draw_text(ren, x0, fy, 1, gray, "Enter=rebind  Del=clear  Tab=device  R=reset  Esc=save+close");

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}
