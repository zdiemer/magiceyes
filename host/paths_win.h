/* paths_win -- native Win32 window to relocate magiceyes' portable storage dirs
 * (Settings / Firmware / Cache) on the Windows bundle.
 *
 * Mirrors keybind_win.c: a real OS window (not the in-SDL overlay), modeless, owned by the SDL
 * window, living on the viewer thread (where the Win32 menu + SDL pump already run). Each row has
 * an Explorer "Browse..." folder picker; choices persist to <exe_dir>/paths.conf via me_paths_set
 * (host/engine/paths.h) and a Settings change re-points input_config live. Windows-only. */
#ifndef PATHS_WIN_H
#define PATHS_WIN_H
#ifdef _WIN32
#include <windows.h>

void paths_win_open(HWND parent);   /* create + show (or raise if already open) */
int  paths_win_is_open(void);       /* viewer pauses game input while nonzero */
void paths_win_close(void);         /* destroy (also invoked by the window's close box) */

#endif /* _WIN32 */
#endif /* PATHS_WIN_H */
