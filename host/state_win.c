/* state_win -- native Win32 savestate slot picker. See state_win.h. */
#ifdef _WIN32
#include "state_win.h"
#include "state_file.h"

#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The engine's per-title slot path. Declared here rather than included, for the same reason
   viewer.c hand-declares its engine entry points: this file must not pull in engine.h. */
int me_state_slot_path_for_current(int slot, char *out, size_t cap);

enum { IDC_LIST = 300, IDC_SAVE, IDC_LOAD, IDC_DELETE, IDC_CLOSE };

#define NROWS (ME_STATE_NSLOTS + 1)      /* quick + 1..9 */

static HWND s_hwnd, s_list, s_status;
static void (*s_do_save)(int slot);
static void (*s_do_load)(int slot);

/* One row's cached metadata, so WM_PAINT and the buttons do not re-read the disk. */
static struct slotinfo {
    int      present;
    int64_t  save_time;
    uint32_t frame_seq;
    long     bytes;
    uint16_t tw, th;
    uint8_t *thumb;      /* RGB565, tw*th*2, malloc'd */
    char     err[64];
} s_slot[NROWS];

static void slot_free(struct slotinfo *s) { free(s->thumb); memset(s, 0, sizeof *s); }

static void lv_set(int row, int col, const char *text) {
    LVITEMA it; memset(&it, 0, sizeof it);
    it.mask = LVIF_TEXT; it.iItem = row; it.iSubItem = col; it.pszText = (LPSTR)text;
    SendMessageA(s_list, col ? LVM_SETITEMTEXTA : LVM_SETITEMA, row, (LPARAM)&it);
}

/* "3 minutes ago" beats a timestamp for the thing this list is actually for: telling two
   savestates apart at a glance. The absolute time is in the status line when a row is selected. */
static void human_age(int64_t when, char *out, size_t cap) {
    long long secs = (long long)time(NULL) - (long long)when;
    if (secs < 0) secs = 0;
    if      (secs < 60)    snprintf(out, cap, "%llds ago", secs);
    else if (secs < 3600)  snprintf(out, cap, "%lldm ago", secs / 60);
    else if (secs < 86400) snprintf(out, cap, "%lldh ago", secs / 3600);
    else                   snprintf(out, cap, "%lldd ago", secs / 86400);
}

static void read_slots(void) {
    for (int i = 0; i < NROWS; i++) {
        slot_free(&s_slot[i]);
        char path[1024];
        if (me_state_slot_path_for_current(i, path, sizeof path) != 0) continue;
        struct mst_info info;
        uint8_t *thumb = NULL; size_t tlen = 0;
        int rc = mst_probe(path, &info, NULL, NULL, &thumb, &tlen);
        if (rc != MST_OK) {
            free(thumb);
            /* MST_ERR_IO just means "no state in that slot"; anything else is a state that is
               there but unreadable, which the user should be told about rather than shown as
               empty. */
            if (rc != MST_ERR_IO) snprintf(s_slot[i].err, sizeof s_slot[i].err, "%s", mst_strerror(rc));
            continue;
        }
        s_slot[i].present = 1;
        s_slot[i].save_time = info.save_time;
        s_slot[i].frame_seq = info.frame_seq;
        s_slot[i].tw = info.thumb_w; s_slot[i].th = info.thumb_h;
        if (thumb && tlen >= (size_t)info.thumb_w * info.thumb_h * 2) s_slot[i].thumb = thumb;
        else free(thumb);
        FILE *f = fopen(path, "rb");
        if (f) { fseek(f, 0, SEEK_END); s_slot[i].bytes = ftell(f); fclose(f); }
    }
}

static void fill_list(void) {
    SendMessageA(s_list, LVM_DELETEALLITEMS, 0, 0);
    for (int i = 0; i < NROWS; i++) {
        char name[24];
        snprintf(name, sizeof name, "%s", i == ME_STATE_SLOT_QUICK ? "Quick" : me_state_slot_name(i));
        LVITEMA it; memset(&it, 0, sizeof it);
        it.mask = LVIF_TEXT; it.iItem = i; it.pszText = name;
        SendMessageA(s_list, LVM_INSERTITEMA, 0, (LPARAM)&it);
        if (s_slot[i].present) {
            char age[32], frame[24], size[24];
            human_age(s_slot[i].save_time, age, sizeof age);
            snprintf(frame, sizeof frame, "%u", s_slot[i].frame_seq);
            snprintf(size, sizeof size, "%.1f MB", s_slot[i].bytes / 1048576.0);
            lv_set(i, 1, age); lv_set(i, 2, frame); lv_set(i, 3, size);
        } else {
            lv_set(i, 1, s_slot[i].err[0] ? s_slot[i].err : "(empty)");
            lv_set(i, 2, ""); lv_set(i, 3, "");
        }
    }
}

static int selected_row(void) {
    int r = (int)SendMessageA(s_list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    return (r >= 0 && r < NROWS) ? r : -1;
}

static void select_row(int row) {
    LVITEMA it; memset(&it, 0, sizeof it);
    it.mask = LVIF_STATE; it.state = LVIS_SELECTED | LVIS_FOCUSED;
    it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageA(s_list, LVM_SETITEMSTATE, row, (LPARAM)&it);
}

/* The preview pane: the saved screen, nearest-neighbour scaled to fit. RGB565 goes straight to
   StretchDIBits via a BITMAPV4HEADER with explicit channel masks -- which is the whole reason
   the thumbnail is stored raw rather than as a PNG, since nothing in host/ can decode one. */
static const int PREV_X = 396, PREV_Y = 44, PREV_W = 200, PREV_H = 150;

static void paint_preview(HDC hdc) {
    RECT box = { PREV_X, PREV_Y, PREV_X + PREV_W, PREV_Y + PREV_H };
    FillRect(hdc, &box, (HBRUSH)GetStockObject(BLACK_BRUSH));
    int row = selected_row();
    if (row < 0 || !s_slot[row].thumb || !s_slot[row].tw || !s_slot[row].th) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(160, 160, 160));
        const char *msg = (row >= 0 && s_slot[row].present) ? "(no preview)" : "(empty slot)";
        DrawTextA(hdc, msg, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    int tw = s_slot[row].tw, th = s_slot[row].th;
    /* letterbox rather than stretch: a distorted preview is harder to recognise */
    int sc = PREV_W * th < PREV_H * tw ? PREV_W * 1000 / tw : PREV_H * 1000 / th;
    int dw = tw * sc / 1000, dh = th * sc / 1000;
    int dx = PREV_X + (PREV_W - dw) / 2, dy = PREV_Y + (PREV_H - dh) / 2;

    BITMAPV4HEADER bi; memset(&bi, 0, sizeof bi);
    bi.bV4Size = sizeof bi;
    bi.bV4Width = tw;
    bi.bV4Height = -th;                  /* negative: our rows are top-down */
    bi.bV4Planes = 1;
    bi.bV4BitCount = 16;
    bi.bV4V4Compression = BI_BITFIELDS;
    bi.bV4RedMask   = 0xF800;
    bi.bV4GreenMask = 0x07E0;
    bi.bV4BlueMask  = 0x001F;
    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc, dx, dy, dw, dh, 0, 0, tw, th,
                  s_slot[row].thumb, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
}

static void update_status(void) {
    int row = selected_row();
    char msg[256];
    if (row < 0) {
        snprintf(msg, sizeof msg, "Select a slot.");
    } else if (!s_slot[row].present) {
        snprintf(msg, sizeof msg, "Slot %s is empty. Save writes the current moment into it.",
                 row == ME_STATE_SLOT_QUICK ? "Quick" : me_state_slot_name(row));
    } else {
        time_t tt = (time_t)s_slot[row].save_time;
        struct tm tmv;
        char when[64] = "";
        if (localtime_s(&tmv, &tt) == 0) strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tmv);
        snprintf(msg, sizeof msg, "Saved %s at frame %u.", when, s_slot[row].frame_seq);
    }
    SetWindowTextA(s_status, msg);
    /* Load and Delete only mean something for a slot that has something in it. */
    EnableWindow(GetDlgItem(s_hwnd, IDC_LOAD), row >= 0 && s_slot[row].present);
    EnableWindow(GetDlgItem(s_hwnd, IDC_DELETE), row >= 0 && s_slot[row].present);
    EnableWindow(GetDlgItem(s_hwnd, IDC_SAVE), row >= 0);
    InvalidateRect(s_hwnd, NULL, FALSE);
}

static void do_delete(int row) {
    char path[1024];
    if (me_state_slot_path_for_current(row, path, sizeof path) != 0) return;
    char q[256];
    snprintf(q, sizeof q, "Delete the savestate in slot %s?\n\nThis cannot be undone.",
             row == ME_STATE_SLOT_QUICK ? "Quick" : me_state_slot_name(row));
    if (MessageBoxA(s_hwnd, q, "magiceyes", MB_OKCANCEL | MB_ICONWARNING) != IDOK) return;
    remove(path);
    state_win_refresh();
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        paint_preview(hdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        int row = selected_row();
        if (id == IDC_CLOSE)  { state_win_close(); return 0; }
        if (id == IDC_SAVE && row >= 0 && s_do_save) { s_do_save(row); state_win_refresh(); return 0; }
        if (id == IDC_LOAD && row >= 0 && s_do_load && s_slot[row].present) {
            /* Close first: the load tears the guest down and rebuilds it, and leaving a window
               open over that is asking for the user to click Load twice. */
            void (*load)(int) = s_do_load;
            state_win_close();
            load(row);
            return 0;
        }
        if (id == IDC_DELETE && row >= 0 && s_slot[row].present) { do_delete(row); return 0; }
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->idFrom == IDC_LIST && nh->code == LVN_ITEMCHANGED) { update_status(); return 0; }
        if (nh->idFrom == IDC_LIST && nh->code == NM_DBLCLK) {
            int row = selected_row();
            if (row >= 0 && s_slot[row].present && s_do_load) {
                void (*load)(int) = s_do_load;
                state_win_close();
                load(row);
            }
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:   state_win_close(); return 0;
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
    wc.lpszClassName = "MagiceyesStates";
    RegisterClassA(&wc);
}

static HWND mk_button(HWND parent, HINSTANCE inst, int id, const char *text, int x, int y, int w, int hh) {
    return CreateWindowA("BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                         x, y, w, hh, parent, (HMENU)(INT_PTR)id, inst, NULL);
}

void state_win_open(HWND parent, int cur_slot,
                    void (*do_save)(int slot), void (*do_load)(int slot)) {
    if (s_hwnd) { SetForegroundWindow(s_hwnd); return; }
    s_do_save = do_save; s_do_load = do_load;

    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    HINSTANCE inst = (HINSTANCE)GetModuleHandle(NULL);
    register_class(inst);

    const int CW = 620, CH = 300;
    RECT rc = { 0, 0, CW, CH };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    s_hwnd = CreateWindowA("MagiceyesStates", "magiceyes -- Savestates", style,
                           CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                           parent, NULL, inst, NULL);
    if (!s_hwnd) return;

    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    CreateWindowA("STATIC", "Slots for the running game:", WS_CHILD | WS_VISIBLE,
                  12, 15, 260, 18, s_hwnd, NULL, inst, NULL);

    s_list = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, NULL,
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                  12, 44, 372, CH - 44 - 76, s_hwnd, (HMENU)(INT_PTR)IDC_LIST, inst, NULL);
    SendMessageA(s_list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    { LVCOLUMNA col; memset(&col, 0, sizeof col); col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
      struct { const char *t; int w; } c[] = { { "Slot", 64 }, { "Saved", 120 },
                                               { "Frame", 88 }, { "Size", 84 } };
      for (int i = 0; i < 4; i++) { col.iSubItem = i; col.pszText = (LPSTR)c[i].t; col.cx = c[i].w;
          SendMessageA(s_list, LVM_INSERTCOLUMNA, i, (LPARAM)&col); } }

    int by = CH - 64, bh = 26;
    mk_button(s_hwnd, inst, IDC_SAVE,   "Save",   12,  by, 80, bh);
    mk_button(s_hwnd, inst, IDC_LOAD,   "Load",   98,  by, 80, bh);
    mk_button(s_hwnd, inst, IDC_DELETE, "Delete", 184, by, 80, bh);
    mk_button(s_hwnd, inst, IDC_CLOSE,  "Close",  CW - 92, by, 80, bh);

    s_status = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE,
                  12, CH - 28, CW - 24, 18, s_hwnd, NULL, inst, NULL);

    for (HWND c = GetWindow(s_hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageA(c, WM_SETFONT, (WPARAM)font, TRUE);

    read_slots();
    fill_list();
    select_row(cur_slot >= 0 && cur_slot < NROWS ? cur_slot : 0);
    update_status();
    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);
}

int state_win_is_open(void) { return s_hwnd != NULL; }

void state_win_refresh(void) {
    if (!s_hwnd) return;
    int row = selected_row();
    read_slots();
    fill_list();
    if (row >= 0) select_row(row);
    update_status();
}

void state_win_close(void) {
    if (!s_hwnd) return;
    HWND h = s_hwnd;
    s_hwnd = NULL;                       /* clear first: DestroyWindow re-enters the proc */
    DestroyWindow(h);
    for (int i = 0; i < NROWS; i++) slot_free(&s_slot[i]);
}

#endif /* _WIN32 */
