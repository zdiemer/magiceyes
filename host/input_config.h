/* input_config -- user-remappable, per-device input bindings for the SDL2 viewer.
 *
 * The viewer always writes the CANONICAL GP2X button bitmap into shm->buttons (the
 * GP2X_* enum in gp2xshm.h); the guest shim handles device-native reordering/axes
 * (guest/src/fakesdl.c). So a "binding" maps host inputs (keyboard scancodes, gamepad
 * buttons, gamepad axes) onto canonical GP2X bits, with a separate profile per device
 * (GP2X / Wiz / Caanoo). Defaults reproduce the old hardcoded keymap + add gamepad and
 * volume-button bindings. Persisted to <appdata>/magiceyes/bindings.conf.
 *
 * Cross-platform and engine-independent: links into the bundle AND the standalone
 * viewer.exe / viewer (which do NOT link the engine), so it must not reference engine
 * symbols (e.g. me_writable_root) -- it derives its own config path. */
#ifndef INPUT_CONFIG_H
#define INPUT_CONFIG_H

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stddef.h>
#include "gp2xshm.h"   /* GP2X_* enum, GP2X_NBUTTONS */

#define IC_NDEV        3       /* 0=GP2X 1=Wiz 2=Caanoo (matches shm->device) */
#define IC_MAX_SRC     4       /* max input sources bound to one GP2X button */
#define IC_AXIS_THRESH 16000   /* analog-axis trigger magnitude (|value| > this) */

typedef enum { IC_SRC_NONE = 0, IC_SRC_KEY, IC_SRC_CBTN, IC_SRC_AXIS } ic_srctype;

typedef struct {
    uint8_t type;   /* ic_srctype */
    int16_t code;   /* KEY: SDL_Scancode | CBTN: SDL_GameControllerButton | AXIS: SDL_GameControllerAxis */
    int8_t  dir;    /* AXIS only: -1 or +1 (which half of the axis triggers) */
} ic_source;

typedef struct { ic_source src[IC_MAX_SRC]; int nsrc; } ic_binding;
typedef struct { ic_binding btn[GP2X_NBUTTONS]; } ic_profile;   /* indexed by GP2X_* bit */
typedef struct { ic_profile prof[IC_NDEV]; } ic_config;

/* The user-bindable GP2X buttons, in UI display order. Excludes the 4 computed diagonals
   (UPLEFT/UPRIGHT/DOWNLEFT/DOWNRIGHT), which ic_compute_buttons derives from the cardinals. */
extern const int  ic_bindable[];   /* GP2X_* bits */
extern const int  ic_nbindable;

void ic_load_defaults(ic_config *c);            /* fill all IC_NDEV profiles with built-ins */
int  ic_load(ic_config *c);                     /* defaults, then overlay bindings.conf if present (1 if file read) */
int  ic_save(const ic_config *c);               /* write bindings.conf (0 on success) */
void ic_reset_device(ic_config *c, int device); /* restore one profile to its defaults */

/* OR each canonical bit whose binding has an active source, then derive the 8-way diagonals
   from the 4 cardinals (same rule the old hardcoded viewer block used). */
uint32_t ic_compute_buttons(const ic_config *c, const Uint8 *kbd,
                            SDL_GameController **pads, int npads, int device);

/* UI / file-format helpers */
const char *ic_button_name(int gp2x_bit);       /* "UP","A","VOLUP",... ("" if not bindable) */
void ic_source_describe(const ic_source *s, char *out, size_t cap);  /* human, e.g. "Z" / "Pad:A" / "Pad:LeftX+" */
void ic_binding_describe(const ic_binding *b, char *out, size_t cap);/* sources joined with ", " ("(unbound)" if none) */

#endif /* INPUT_CONFIG_H */
