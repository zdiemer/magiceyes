/* state_win -- native Win32 savestate slot picker for the Windows bundle (magiceyes.exe).
 *
 * Ten rows (Quick, then 1..9) with when each was saved, at which frame, and how big; a preview
 * of the saved screen beside them; and Save / Load / Delete.
 *
 * It reads slots through the CONTAINER (host/state_file.h) rather than through the engine, so
 * drawing the list costs one header, one META chunk and one thumbnail per slot and never touches
 * the multi-megabyte body. Save and Load go back through the viewer's own state_request(), so
 * the picker and the F5/F8 hotkeys take exactly the same path.
 *
 * Modeless and owned by the SDL window, like keybind_win.c: it lives on the viewer thread, where
 * the Win32 menu and the SDL pump already run, so there is no cross-thread hop and no private
 * message loop. Non-Windows builds use the settings_ui.c states page instead. */
#ifndef STATE_WIN_H
#define STATE_WIN_H
#ifdef _WIN32
#include <windows.h>

/* Create + show the picker (or raise it if already open). `cur_slot` is the slot F6/F7 selected,
   which the list starts on. The two callbacks are the viewer's, so the picker never has to know
   whether a request goes through the engine directly or through the shm bytes. */
void state_win_open(HWND parent, int cur_slot,
                    void (*do_save)(int slot), void (*do_load)(int slot));
int  state_win_is_open(void);   /* the viewer pauses game input while this is nonzero */
void state_win_close(void);
void state_win_refresh(void);   /* re-read the slots (after a save, or a slot change) */

#endif /* _WIN32 */
#endif /* STATE_WIN_H */
