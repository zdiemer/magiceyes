/* paths_win -- see paths_win.h. Win32 implementation.
 *
 * A tabbed Settings window: "Storage" (relocate the portable Settings/Firmware/Cache dirs) and
 * "Audio" (volume slider + mute, persisted by the viewer). The tab control is purely visual --
 * each page's child controls are owned by the window and shown/hidden on the active tab. */
#ifdef _WIN32
#include "paths_win.h"
#include "paths.h"
#include <commctrl.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

/* input_config + the viewer's audio prefs live in the bundle but pull in SDL; we only need a few
   setters, so forward-declare them rather than include their headers (avoids dragging SDL in). */
extern void ic_set_config_dir(const char *dir);
extern int  me_view_get_volume(void);
extern int  me_view_get_mute(void);
extern void me_view_set_volume(int v);
extern void me_view_set_mute(int on);

/* one-line description shown above each path's edit box */
static const char *row_desc[ME_PATH_NKINDS] = {
    "Settings  --  keybindings, recent list, games-folder pointer",
    "Firmware  --  installed device firmware (Wiz / GP2X F100/F200 / Caanoo)",
    "Cache  --  decompressed games + extracted archives (safe to delete anytime)",
};

enum { IDC_BROWSE0 = 3001, IDC_RESET = 3100, IDC_CLOSE = 3101,
       IDC_TAB = 3200, IDC_VOL = 3201, IDC_MUTE = 3202 };

static HWND s_hwnd, s_tab, s_close;
static HWND s_edit[ME_PATH_NKINDS], s_status;
static HWND s_vol, s_volpct;                 /* Audio page: trackbar + "NN%" label */
/* per-page child lists for show/hide on tab switch */
static HWND s_page0[16]; static int s_n0;    /* Storage */
static HWND s_page1[8];  static int s_n1;    /* Audio   */

static void set_status(const char *s) { if (s_status) SetWindowTextA(s_status, s); }

/* Show each kind's currently-resolved dir (me_paths_dir creates it; it also normalises the
   separators on Windows, so the boxes never show mixed slashes). */
static void refresh(void) {
    for (int k = 0; k < ME_PATH_NKINDS; k++) {
        char dir[1024]; me_paths_dir((me_path_kind)k, dir, sizeof dir);
        SetWindowTextA(s_edit[k], dir);
    }
}

static void set_vol_label(int v) { char b[16]; snprintf(b, sizeof b, "%d%%", v); SetWindowTextA(s_volpct, b); }

static void show_page(int pg) {
    for (int i = 0; i < s_n0; i++) ShowWindow(s_page0[i], pg == 0 ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < s_n1; i++) ShowWindow(s_page1[i], pg == 1 ? SW_SHOW : SW_HIDE);
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
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lp;
        if (nm->hwndFrom == s_tab && nm->code == TCN_SELCHANGE) {
            show_page(TabCtrl_GetCurSel(s_tab)); return 0;
        }
        break;
    }
    case WM_HSCROLL:
        if ((HWND)lp == s_vol) {
            int v = (int)SendMessageA(s_vol, TBM_GETPOS, 0, 0);
            me_view_set_volume(v); set_vol_label(v);
            return 0;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_MUTE:
            me_view_set_mute(SendMessageA((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED);
            return 0;
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
    INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_TAB_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&ic);
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

    const int CW = 660;
    const int TABY = 10, TABH = 252;            /* tab control geometry */
    const int PX = 24, PY = 48, PW = CW - 48;   /* page content area (inside the tab) */
    const int CH = TABY + TABH + 48;            /* + Close button row */
    RECT rc = { 0, 0, CW, CH };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    s_hwnd = CreateWindowA("MagiceyesPaths", "magiceyes -- Settings", style,
                           CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                           parent, NULL, inst, NULL);
    if (!s_hwnd) return;

    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    s_n0 = s_n1 = 0;

    s_tab = CreateWindowA(WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                          TABY, TABY, CW - 2 * TABY, TABH, s_hwnd, (HMENU)(INT_PTR)IDC_TAB, inst, NULL);
    { TCITEMA ti; memset(&ti, 0, sizeof ti); ti.mask = TCIF_TEXT;
      ti.pszText = (LPSTR)"Storage"; SendMessageA(s_tab, TCM_INSERTITEMA, 0, (LPARAM)&ti);
      ti.pszText = (LPSTR)"Audio";   SendMessageA(s_tab, TCM_INSERTITEMA, 1, (LPARAM)&ti); }

    /* ---- Storage page ---- */
    s_page0[s_n0++] = CreateWindowA("STATIC",
        "magiceyes is portable: by default these live beside the .exe. Move any of them here.",
        WS_CHILD | WS_VISIBLE, PX, PY, PW, 18, s_hwnd, NULL, inst, NULL);
    const int ROWTOP = PY + 26, ROWH = 56;
    for (int k = 0; k < ME_PATH_NKINDS; k++) {
        int y = ROWTOP + k * ROWH;
        s_page0[s_n0++] = CreateWindowA("STATIC", row_desc[k], WS_CHILD | WS_VISIBLE,
                      PX, y, PW, 16, s_hwnd, NULL, inst, NULL);
        s_edit[k] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                      PX, y + 18, PW - 98, 22, s_hwnd, NULL, inst, NULL);
        s_page0[s_n0++] = s_edit[k];
        s_page0[s_n0++] = mk_button(s_hwnd, inst, IDC_BROWSE0 + k, "Browse...", PX + PW - 90, y + 17, 90, 24);
    }
    int sby = ROWTOP + ME_PATH_NKINDS * ROWH + 4;
    s_page0[s_n0++] = mk_button(s_hwnd, inst, IDC_RESET, "Reset to defaults", PX, sby, 140, 26);
    s_status = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE, PX + 152, sby + 5, PW - 152, 18,
                             s_hwnd, NULL, inst, NULL);
    s_page0[s_n0++] = s_status;

    /* ---- Audio page ---- */
    s_page1[s_n1++] = CreateWindowA("STATIC", "Master volume", WS_CHILD,
        PX, PY, 200, 18, s_hwnd, NULL, inst, NULL);
    s_vol = CreateWindowA(TRACKBAR_CLASSA, "", WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        PX, PY + 24, 320, 32, s_hwnd, (HMENU)(INT_PTR)IDC_VOL, inst, NULL);
    SendMessageA(s_vol, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageA(s_vol, TBM_SETTICFREQ, 10, 0);
    SendMessageA(s_vol, TBM_SETPOS, TRUE, me_view_get_volume());
    s_page1[s_n1++] = s_vol;
    s_volpct = CreateWindowA("STATIC", "", WS_CHILD, PX + 332, PY + 30, 60, 18, s_hwnd, NULL, inst, NULL);
    s_page1[s_n1++] = s_volpct;
    set_vol_label(me_view_get_volume());
    { HWND mute = CreateWindowA("BUTTON", "Mute", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
        PX, PY + 70, 200, 22, s_hwnd, (HMENU)(INT_PTR)IDC_MUTE, inst, NULL);
      SendMessageA(mute, BM_SETCHECK, me_view_get_mute() ? BST_CHECKED : BST_UNCHECKED, 0);
      s_page1[s_n1++] = mute; }

    /* ---- shared Close button (outside the tab) ---- */
    s_close = mk_button(s_hwnd, inst, IDC_CLOSE, "Close", CW - 92, TABY + TABH + 12, 80, 26);

    for (HWND c = GetWindow(s_hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageA(c, WM_SETFONT, (WPARAM)font, TRUE);

    refresh();
    show_page(0);
    ShowWindow(s_hwnd, SW_SHOW);
    SetForegroundWindow(s_hwnd);
}

int paths_win_is_open(void) { return s_hwnd != NULL; }

void paths_win_close(void) {
    if (!s_hwnd) return;
    HWND h = s_hwnd; s_hwnd = NULL;
    memset(s_edit, 0, sizeof s_edit); s_status = NULL;
    s_tab = s_vol = s_volpct = s_close = NULL; s_n0 = s_n1 = 0;
    DestroyWindow(h);
}

#endif /* _WIN32 */
