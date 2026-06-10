/* keybind_win -- native Win32 keybinding editor for the Windows bundle (magiceyes.exe).
 *
 * Replaces the in-SDL settings overlay (settings_ui.c) on Windows with a real OS window:
 * a system dropdown (GP2X / Wiz / Caanoo) over a 3-column table -- the device's native
 * button, the mapped keyboard key, and the mapped controller input -- with click-to-rebind.
 *
 * It edits the SAME ic_config the viewer uses (host inputs -> canonical GP2X bits) and
 * persists with ic_save on close, so the runtime input path is unchanged. Modeless and
 * owned by the SDL window; it lives on the viewer thread (where the Win32 menu + SDL pump
 * already run), so no cross-thread hop is needed. The Linux/WSL viewer and the standalone
 * two-process viewer.exe keep using settings_ui.c (no native toolkit linked there). */
#ifndef KEYBIND_WIN_H
#define KEYBIND_WIN_H
#ifdef _WIN32
#include <windows.h>
#include "input_config.h"

/* Create + show the editor (or raise it if already open). cfg is borrowed, not owned. */
void kbwin_open(HWND parent, ic_config *cfg, int device);
int  kbwin_is_open(void);   /* viewer pauses game input while this is nonzero */
void kbwin_close(void);     /* destroy + ic_save (also invoked by the window's close box) */

#endif /* _WIN32 */
#endif /* KEYBIND_WIN_H */
