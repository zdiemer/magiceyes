/* keybind_win -- see keybind_win.h. Win32 (comctl32) implementation. */
#ifdef _WIN32
#include "keybind_win.h"
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "gp2xshm.h"

/* The viewer owns the open game controllers; we poll them during controller capture. */
extern SDL_GameController *g_pads[];
extern int g_npads;

/* ---- per-device button rows: display label -> canonical GP2X bit (gp2xshm.h) ----
   The viewer always writes canonical GP2X bits and the guest shim reorders per device, so a
   row maps the device's PHYSICAL control name onto the canonical bit the binding feeds. */
typedef struct { const char *label; int bit; } kb_row;

static const kb_row rows_gp2x[] = {   /* dir-pad (analog on F100, d-pad on F200), abxy, sel/start, vol, l/r */
    { "D-Pad Up", GP2X_UP }, { "D-Pad Down", GP2X_DOWN }, { "D-Pad Left", GP2X_LEFT }, { "D-Pad Right", GP2X_RIGHT },
    { "A", GP2X_A }, { "B", GP2X_B }, { "X", GP2X_X }, { "Y", GP2X_Y },
    { "Select", GP2X_SELECT }, { "Start", GP2X_START },
    { "Vol -", GP2X_VOLDOWN }, { "Vol +", GP2X_VOLUP },
    { "L", GP2X_L }, { "R", GP2X_R },
};
static const kb_row rows_wiz[] = {    /* dir-pad, abxy, sel/menu, vol, l/r */
    { "D-Pad Up", GP2X_UP }, { "D-Pad Down", GP2X_DOWN }, { "D-Pad Left", GP2X_LEFT }, { "D-Pad Right", GP2X_RIGHT },
    { "A", GP2X_A }, { "B", GP2X_B }, { "X", GP2X_X }, { "Y", GP2X_Y },
    { "Select", GP2X_SELECT }, { "Menu", GP2X_START },
    { "Vol -", GP2X_VOLDOWN }, { "Vol +", GP2X_VOLUP },
    { "L", GP2X_L }, { "R", GP2X_R },
};
static const kb_row rows_caanoo[] = { /* analog stick, abxy, home, I/II, l/r */
    { "Stick Up", GP2X_UP }, { "Stick Down", GP2X_DOWN }, { "Stick Left", GP2X_LEFT }, { "Stick Right", GP2X_RIGHT },
    { "A", GP2X_A }, { "B", GP2X_B }, { "X", GP2X_X }, { "Y", GP2X_Y },
    { "Home", GP2X_START },
    { "I", GP2X_VOLUP }, { "II", GP2X_VOLDOWN },
    { "L", GP2X_L }, { "R", GP2X_R },
};
static const kb_row *rows_for(int dev, int *n) {
    switch (dev) {
    case 1: *n = (int)(sizeof rows_wiz / sizeof rows_wiz[0]);       return rows_wiz;
    case 2: *n = (int)(sizeof rows_caanoo / sizeof rows_caanoo[0]); return rows_caanoo;
    default:*n = (int)(sizeof rows_gp2x / sizeof rows_gp2x[0]);     return rows_gp2x;
    }
}

/* ---- window + control state ---- */
enum { IDC_COMBO = 2001, IDC_LIST, IDC_SETKEY, IDC_SETPAD, IDC_CLEAR, IDC_RESET, IDC_CLOSE };
enum { CAP_NONE = 0, CAP_KEY, CAP_PAD };
#define KBWIN_TIMER 1

static HWND       s_hwnd, s_combo, s_list, s_status, s_setkey, s_setpad, s_clear;
static ic_config *s_cfg;
static int        s_device;
static int        s_capture;   /* CAP_* */
static int        s_caprow;    /* ic_bindable row being captured (index into rows_for table) */

/* category test: a "controller" source is a pad button or a pad axis; otherwise it's a key. */
static int src_is_pad(const ic_source *s) { return s->type == IC_SRC_CBTN || s->type == IC_SRC_AXIS; }

static ic_binding *row_binding(int row) {
    int n; const kb_row *r = rows_for(s_device, &n);
    if (row < 0 || row >= n) return NULL;
    return &s_cfg->prof[s_device].btn[r[row].bit];
}

/* Replace every source of one category (keyboard XOR controller) with ns, keeping the other
   category -- so rebinding the key leaves the controller binding intact, and vice versa. */
static void bind_set(ic_binding *b, const ic_source *ns, int pad_cat) {
    int n = 0;
    for (int i = 0; i < b->nsrc; i++)
        if (src_is_pad(&b->src[i]) != pad_cat) b->src[n++] = b->src[i];
    b->nsrc = n;
    if (b->nsrc < IC_MAX_SRC) b->src[b->nsrc++] = *ns;
}

/* Join the sources of one category for a table cell ("Z" / "Backspace, RShift" / "A, LeftX+"). */
static void col_text(const ic_binding *b, int pad_cat, char *out, size_t cap) {
    size_t o = 0; int first = 1; out[0] = 0;
    for (int i = 0; i < b->nsrc && o + 1 < cap; i++) {
        if (src_is_pad(&b->src[i]) != pad_cat) continue;
        char one[64]; ic_source_describe(&b->src[i], one, sizeof one);
        const char *p = one;
        if (pad_cat && !strncmp(p, "Pad:", 4)) p += 4;   /* the "Pad:" prefix is redundant in the controller column */
        o += (size_t)snprintf(out + o, cap - o, "%s%s", first ? "" : ", ", p); first = 0;
    }
    if (first) snprintf(out, cap, "%s", "(unbound)");
}

static void lv_set(int row, int col, const char *text) {
    LVITEMA it; memset(&it, 0, sizeof it);
    it.iSubItem = col; it.pszText = (LPSTR)text;
    SendMessageA(s_list, LVM_SETITEMTEXTA, row, (LPARAM)&it);
}

static void refresh_row(int row) {
    ic_binding *b = row_binding(row); if (!b) return;
    char kb[160], pad[160];
    col_text(b, 0, kb, sizeof kb);
    col_text(b, 1, pad, sizeof pad);
    lv_set(row, 1, kb);
    lv_set(row, 2, pad);
}

static void fill_list(void) {
    SendMessageA(s_list, LVM_DELETEALLITEMS, 0, 0);
    int n; const kb_row *r = rows_for(s_device, &n);
    for (int i = 0; i < n; i++) {
        LVITEMA it; memset(&it, 0, sizeof it);
        it.mask = LVIF_TEXT; it.iItem = i; it.pszText = (LPSTR)r[i].label;
        SendMessageA(s_list, LVM_INSERTITEMA, 0, (LPARAM)&it);
        refresh_row(i);
    }
}

static void set_status(const char *s) { SetWindowTextA(s_status, s); }

static void cancel_capture(void) {
    if (s_capture == CAP_PAD) KillTimer(s_hwnd, KBWIN_TIMER);
    s_capture = CAP_NONE; s_caprow = -1;
    EnableWindow(s_combo, TRUE); EnableWindow(s_list, TRUE);
    set_status("Double-click a row to rebind its key. Use the buttons for controller / clear.");
    SetFocus(s_list);
}

static int selected_row(void) { return (int)SendMessageA(s_list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED); }

static void begin_capture(int kind, int row) {
    if (row < 0) { set_status("Select a row first."); return; }
    s_capture = kind; s_caprow = row;
    int n; const kb_row *r = rows_for(s_device, &n);
    char msg[160];
    snprintf(msg, sizeof msg, "Press a %s for \"%s\"  (Esc to cancel)",
             kind == CAP_KEY ? "key" : "controller button or stick", r[row].label);
    set_status(msg);
    EnableWindow(s_combo, FALSE);   /* don't let the device change mid-capture */
    if (kind == CAP_KEY) {
        SetFocus(s_hwnd);           /* route WM_KEYDOWN to our proc, not the list */
    } else {
        SetTimer(s_hwnd, KBWIN_TIMER, 16, NULL);   /* poll the pads */
    }
}

static void commit_source(const ic_source *ns, int pad_cat) {
    ic_binding *b = row_binding(s_caprow);
    int row = s_caprow;
    if (b) { bind_set(b, ns, pad_cat); }
    cancel_capture();
    if (b) refresh_row(row);
    ic_save(s_cfg);
}

/* ---- Win32 VK -> SDL scancode (only the keys a remap realistically uses) ---- */
static SDL_Scancode vk_to_scancode(WPARAM wParam, LPARAM lParam) {
    UINT vk = (UINT)wParam;
    UINT scan = (lParam >> 16) & 0xff;
    int  ext  = (lParam >> 24) & 1;
    if (vk == VK_SHIFT)   vk = MapVirtualKey(scan, MAPVK_VSC_TO_VK_EX);   /* L/R shift */
    if (vk == VK_CONTROL) vk = ext ? VK_RCONTROL : VK_LCONTROL;
    if (vk == VK_MENU)    vk = ext ? VK_RMENU    : VK_LMENU;

    if (vk >= 'A' && vk <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (vk - 'A'));
    if (vk >= '1' && vk <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (vk - '1'));
    if (vk == '0')              return SDL_SCANCODE_0;
    if (vk >= VK_F1 && vk <= VK_F12) return (SDL_Scancode)(SDL_SCANCODE_F1 + (vk - VK_F1));
    if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) return (SDL_Scancode)(SDL_SCANCODE_KP_1 + (vk - VK_NUMPAD1));
    switch (vk) {
    case VK_NUMPAD0:  return SDL_SCANCODE_KP_0;
    case VK_LEFT:     return SDL_SCANCODE_LEFT;
    case VK_RIGHT:    return SDL_SCANCODE_RIGHT;
    case VK_UP:       return SDL_SCANCODE_UP;
    case VK_DOWN:     return SDL_SCANCODE_DOWN;
    case VK_RETURN:   return ext ? SDL_SCANCODE_KP_ENTER : SDL_SCANCODE_RETURN;
    case VK_SPACE:    return SDL_SCANCODE_SPACE;
    case VK_TAB:      return SDL_SCANCODE_TAB;
    case VK_BACK:     return SDL_SCANCODE_BACKSPACE;
    case VK_LSHIFT:   return SDL_SCANCODE_LSHIFT;
    case VK_RSHIFT:   return SDL_SCANCODE_RSHIFT;
    case VK_LCONTROL: return SDL_SCANCODE_LCTRL;
    case VK_RCONTROL: return SDL_SCANCODE_RCTRL;
    case VK_LMENU:    return SDL_SCANCODE_LALT;
    case VK_RMENU:    return SDL_SCANCODE_RALT;
    case VK_OEM_MINUS: return SDL_SCANCODE_MINUS;
    case VK_OEM_PLUS:  return SDL_SCANCODE_EQUALS;
    case VK_OEM_COMMA: return SDL_SCANCODE_COMMA;
    case VK_OEM_PERIOD:return SDL_SCANCODE_PERIOD;
    case VK_OEM_1:    return SDL_SCANCODE_SEMICOLON;
    case VK_OEM_2:    return SDL_SCANCODE_SLASH;
    case VK_OEM_3:    return SDL_SCANCODE_GRAVE;
    case VK_OEM_4:    return SDL_SCANCODE_LEFTBRACKET;
    case VK_OEM_5:    return SDL_SCANCODE_BACKSLASH;
    case VK_OEM_6:    return SDL_SCANCODE_RIGHTBRACKET;
    case VK_OEM_7:    return SDL_SCANCODE_APOSTROPHE;
    }
    return SDL_SCANCODE_UNKNOWN;
}

/* Poll the open controllers; bind the first button / out-of-deadzone axis we see. */
static void poll_pad_capture(void) {
    SDL_GameControllerUpdate();
    for (int i = 0; i < g_npads; i++) {
        SDL_GameController *c = g_pads[i]; if (!c) continue;
        for (int btn = 0; btn < SDL_CONTROLLER_BUTTON_MAX; btn++)
            if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)btn)) {
                ic_source ns = { IC_SRC_CBTN, (int16_t)btn, 0 };
                commit_source(&ns, 1); return;
            }
        for (int ax = 0; ax < SDL_CONTROLLER_AXIS_MAX; ax++) {
            int v = SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis)ax);
            if (abs(v) > IC_AXIS_THRESH) {
                ic_source ns = { IC_SRC_AXIS, (int16_t)ax, v > 0 ? (int8_t)+1 : (int8_t)-1 };
                commit_source(&ns, 1); return;
            }
        }
    }
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (wp == KBWIN_TIMER && s_capture == CAP_PAD) poll_pad_capture();
        return 0;

    case WM_KEYDOWN:
        if (s_capture == CAP_NONE) {
            if (wp == VK_ESCAPE) { kbwin_close(); return 0; }
            break;
        }
        if (wp == VK_ESCAPE) { cancel_capture(); return 0; }
        if (s_capture == CAP_KEY) {
            SDL_Scancode sc = vk_to_scancode(wp, lp);
            if (sc != SDL_SCANCODE_UNKNOWN) { ic_source ns = { IC_SRC_KEY, (int16_t)sc, 0 }; commit_source(&ns, 0); }
            return 0;   /* unmapped key: keep waiting */
        }
        return 0;       /* swallow keys during controller capture */

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_COMBO:
            if (HIWORD(wp) == CBN_SELCHANGE && s_capture == CAP_NONE) {
                int sel = (int)SendMessageA(s_combo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < IC_NDEV) { s_device = sel; fill_list(); }
            }
            return 0;
        case IDC_SETKEY: if (s_capture == CAP_NONE) begin_capture(CAP_KEY, selected_row()); return 0;
        case IDC_SETPAD: if (s_capture == CAP_NONE) begin_capture(CAP_PAD, selected_row()); return 0;
        case IDC_CLEAR:
            if (s_capture == CAP_NONE) { int r = selected_row(); ic_binding *b = row_binding(r);
                if (b) { b->nsrc = 0; refresh_row(r); ic_save(s_cfg); } }
            return 0;
        case IDC_RESET:
            if (s_capture == CAP_NONE) {
                if (MessageBoxA(h, "Reset all bindings for this system to defaults?", "magiceyes",
                                MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    ic_reset_device(s_cfg, s_device); fill_list(); ic_save(s_cfg);
                }
            }
            return 0;
        case IDC_CLOSE: kbwin_close(); return 0;
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->idFrom == IDC_LIST && nh->code == NM_DBLCLK && s_capture == CAP_NONE) {
            int r = ((NMITEMACTIVATE *)lp)->iItem;
            if (r >= 0) begin_capture(CAP_KEY, r);
        }
        return 0;
    }

    case WM_CLOSE:    kbwin_close(); return 0;
    case WM_DESTROY:  return 0;
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
    wc.lpszClassName = "MagiceyesKeybind";
    RegisterClassA(&wc);
}

static HWND mk_button(HWND parent, HINSTANCE inst, int id, const char *text, int x, int y, int w, int hh) {
    return CreateWindowA("BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                         x, y, w, hh, parent, (HMENU)(INT_PTR)id, inst, NULL);
}

void kbwin_open(HWND parent, ic_config *cfg, int device) {
    if (s_hwnd) { SetForegroundWindow(s_hwnd); return; }
    s_cfg = cfg;
    s_device = (device >= 0 && device < IC_NDEV) ? device : 0;
    s_capture = CAP_NONE; s_caprow = -1;

    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    HINSTANCE inst = (HINSTANCE)GetModuleHandle(NULL);
    register_class(inst);

    const int CW = 600, CH = 440;
    RECT rc = { 0, 0, CW, CH };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    s_hwnd = CreateWindowA("MagiceyesKeybind", "magiceyes -- Keybindings", style,
                           CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                           parent, NULL, inst, NULL);
    if (!s_hwnd) return;

    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    CreateWindowA("STATIC", "System:", WS_CHILD | WS_VISIBLE,
                  12, 15, 56, 18, s_hwnd, NULL, inst, NULL);
    s_combo = CreateWindowA("COMBOBOX", NULL,
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                  72, 12, 200, 200, s_hwnd, (HMENU)(INT_PTR)IDC_COMBO, inst, NULL);
    static const char *dev_names[IC_NDEV] = { "GP2X", "GP2X Wiz", "GP2X Caanoo" };
    for (int d = 0; d < IC_NDEV; d++) SendMessageA(s_combo, CB_ADDSTRING, 0, (LPARAM)dev_names[d]);
    SendMessageA(s_combo, CB_SETCURSEL, s_device, 0);

    s_list = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, NULL,
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                  12, 44, CW - 24, CH - 44 - 76, s_hwnd, (HMENU)(INT_PTR)IDC_LIST, inst, NULL);
    SendMessageA(s_list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    { LVCOLUMNA col; memset(&col, 0, sizeof col); col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
      struct { const char *t; int w; } c[] = { { "Button", 150 }, { "Keyboard", 200 }, { "Controller", 196 } };
      for (int i = 0; i < 3; i++) { col.iSubItem = i; col.pszText = (LPSTR)c[i].t; col.cx = c[i].w;
          SendMessageA(s_list, LVM_INSERTCOLUMNA, i, (LPARAM)&col); } }

    int by = CH - 64, bh = 26;
    s_setkey = mk_button(s_hwnd, inst, IDC_SETKEY, "Set Key",        12,  by, 90,  bh);
    s_setpad = mk_button(s_hwnd, inst, IDC_SETPAD, "Set Controller", 108, by, 120, bh);
    s_clear  = mk_button(s_hwnd, inst, IDC_CLEAR,  "Clear",          234, by, 70,  bh);
    mk_button(s_hwnd, inst, IDC_RESET, "Reset Defaults", 310, by, 130, bh);
    mk_button(s_hwnd, inst, IDC_CLOSE, "Close",          CW - 92, by, 80, bh);

    s_status = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE,
                  12, CH - 28, CW - 24, 18, s_hwnd, NULL, inst, NULL);

    /* uniform GUI font on every child */
    for (HWND c = GetWindow(s_hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageA(c, WM_SETFONT, (WPARAM)font, TRUE);

    fill_list();
    set_status("Double-click a row to rebind its key. Use the buttons for controller / clear.");
    ShowWindow(s_hwnd, SW_SHOW);
    SetForegroundWindow(s_hwnd);
    SetFocus(s_list);
}

int kbwin_is_open(void) { return s_hwnd != NULL; }

void kbwin_close(void) {
    if (!s_hwnd) return;
    if (s_capture == CAP_PAD) KillTimer(s_hwnd, KBWIN_TIMER);
    ic_save(s_cfg);
    HWND h = s_hwnd; s_hwnd = NULL; s_capture = CAP_NONE;
    s_combo = s_list = s_status = s_setkey = s_setpad = s_clear = NULL;
    DestroyWindow(h);
}

#endif /* _WIN32 */
