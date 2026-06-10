/* paths_win -- see paths_win.h. Win32 implementation. */
#ifdef _WIN32
#include "paths_win.h"
#include "paths.h"
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

/* input_config lives in the bundle but pulls in SDL; we only need this one setter, so forward-
   declare it rather than include input_config.h (avoids dragging SDL headers into this file). */
extern void ic_set_config_dir(const char *dir);

/* one-line description shown above each path's edit box */
static const char *row_desc[ME_PATH_NKINDS] = {
    "Settings  --  keybindings, recent list, games-folder pointer",
    "Firmware  --  installed device firmware (Wiz / GP2X F100/F200 / Caanoo)",
    "Cache  --  decompressed games + extracted archives (safe to delete anytime)",
};

enum { IDC_BROWSE0 = 3001, IDC_RESET = 3100, IDC_CLOSE };

static HWND s_hwnd, s_edit[ME_PATH_NKINDS], s_status;

static void set_status(const char *s) { if (s_status) SetWindowTextA(s_status, s); }

/* Show each kind's currently-resolved dir (me_paths_dir creates it, which is fine -- the dirs
   exist regardless once magiceyes uses them). */
static void refresh(void) {
    for (int k = 0; k < ME_PATH_NKINDS; k++) {
        char dir[1024]; me_paths_dir((me_path_kind)k, dir, sizeof dir);
        SetWindowTextA(s_edit[k], dir);
    }
}

/* Explorer folder picker (same pattern as the viewer's pick_games_folder). 1 on a pick. */
static int pick_folder(HWND owner, const char *title, char *out, size_t cap) {
    wchar_t wtitle[256]; MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);
    BROWSEINFOW bi; memset(&bi, 0, sizeof bi);
    bi.hwndOwner = owner; bi.lpszTitle = wtitle;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return 0;
    int ok = 0; wchar_t wp[MAX_PATH];
    if (SHGetPathFromIDListW(pidl, wp)) {
        WideCharToMultiByte(CP_UTF8, 0, wp, -1, out, (int)cap, NULL, NULL); ok = 1;
    }
    CoTaskMemFree(pidl);
    return ok;
}

static void apply_settings_dir(void) {
    /* keep input_config's bindings.conf in step with the (possibly relocated) Settings dir */
    char dir[1024]; me_paths_dir(ME_PATH_SETTINGS, dir, sizeof dir);
    ic_set_config_dir(dir);
}

static void do_browse(int k) {
    char title[160];
    snprintf(title, sizeof title, "Choose the folder for magiceyes %s files", me_paths_label((me_path_kind)k));
    char picked[1024];
    if (!pick_folder(s_hwnd, title, picked, sizeof picked)) return;
    me_paths_set((me_path_kind)k, picked);
    if (k == ME_PATH_SETTINGS) apply_settings_dir();
    refresh();
    char msg[256];
    snprintf(msg, sizeof msg, "%s folder set. Firmware/Cache moves apply to the next install/launch.",
             me_paths_label((me_path_kind)k));
    set_status(msg);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_RESET:
            if (MessageBoxA(h, "Reset all storage folders to the portable defaults (beside the .exe)?",
                            "magiceyes", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                me_paths_reset(); apply_settings_dir(); refresh();
                set_status("Reset to portable defaults (folders beside the .exe).");
            }
            return 0;
        case IDC_CLOSE: paths_win_close(); return 0;
        default:
            if (LOWORD(wp) >= IDC_BROWSE0 && LOWORD(wp) < IDC_BROWSE0 + ME_PATH_NKINDS)
                do_browse(LOWORD(wp) - IDC_BROWSE0);
            return 0;
        }
    case WM_KEYDOWN: if (wp == VK_ESCAPE) { paths_win_close(); return 0; } break;
    case WM_CLOSE:   paths_win_close(); return 0;
    case WM_DESTROY: return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void register_class(HINSTANCE inst) {
    static int done = 0; if (done) return; done = 1;
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "MagiceyesPaths";
    RegisterClassA(&wc);
}

static HWND mk_button(HWND parent, HINSTANCE inst, int id, const char *text, int x, int y, int w, int hh) {
    return CreateWindowA("BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                         x, y, w, hh, parent, (HMENU)(INT_PTR)id, inst, NULL);
}

void paths_win_open(HWND parent) {
    if (s_hwnd) { SetForegroundWindow(s_hwnd); return; }
    HINSTANCE inst = (HINSTANCE)GetModuleHandle(NULL);
    register_class(inst);

    const int CW = 660, ROWH = 58, TOP = 36;
    const int CH = TOP + ME_PATH_NKINDS * ROWH + 56;
    RECT rc = { 0, 0, CW, CH };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    s_hwnd = CreateWindowA("MagiceyesPaths", "magiceyes -- Settings", style,
                           CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                           parent, NULL, inst, NULL);
    if (!s_hwnd) return;

    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    CreateWindowA("STATIC", "magiceyes is portable: by default these live beside the .exe. Move any of them here.",
                  WS_CHILD | WS_VISIBLE, 12, 10, CW - 24, 18, s_hwnd, NULL, inst, NULL);

    for (int k = 0; k < ME_PATH_NKINDS; k++) {
        int y = TOP + k * ROWH;
        CreateWindowA("STATIC", row_desc[k], WS_CHILD | WS_VISIBLE,
                      12, y, CW - 24, 16, s_hwnd, NULL, inst, NULL);
        s_edit[k] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                      12, y + 18, CW - 24 - 98, 22, s_hwnd, NULL, inst, NULL);
        mk_button(s_hwnd, inst, IDC_BROWSE0 + k, "Browse...", CW - 12 - 90, y + 17, 90, 24);
    }

    int by = CH - 40;
    mk_button(s_hwnd, inst, IDC_RESET, "Reset to defaults", 12, by, 140, 26);
    mk_button(s_hwnd, inst, IDC_CLOSE, "Close", CW - 92, by, 80, 26);
    s_status = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE, 160, by + 5, CW - 160 - 100, 18,
                             s_hwnd, NULL, inst, NULL);

    for (HWND c = GetWindow(s_hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageA(c, WM_SETFONT, (WPARAM)font, TRUE);

    refresh();
    ShowWindow(s_hwnd, SW_SHOW);
    SetForegroundWindow(s_hwnd);
}

int paths_win_is_open(void) { return s_hwnd != NULL; }

void paths_win_close(void) {
    if (!s_hwnd) return;
    HWND h = s_hwnd; s_hwnd = NULL;
    memset(s_edit, 0, sizeof s_edit); s_status = NULL;
    DestroyWindow(h);
}

#endif /* _WIN32 */
