#include "common.h"

/* ==================================================================
   UAH MENU BAR PAINTER (ported from dark-p2, proven per-process).

   Menu bar text (File/View/Options/Help) is painted in win32k KERNEL
   mode (greExtTextOutW) — NO user-mode GDI/text hook can see it.
   Hover repaints go through a user-mode path (hence visible on hover).
   The ONLY user-mode extension point is the undocumented UAH messages
   sent to the top-level window:
     0x0091 (WM_UAHDRAWMENU)     -> paint_uah_menu (bar background)
     0x0092 (WM_UAHDRAWMENUITEM) -> paint_uah_menu_item (per-item text)
   We subclass the top-level window and paint these ourselves with
   dark bg + light text. Non-hovered items become visible.
   ================================================================== */

typedef unsigned long ULONG_PTR_T;

typedef union {
    struct { DWORD cx, cy; } rgsizeBar[2];
    struct { DWORD cx, cy; } rgsizePopup[4];
} UAHMENUITEMMETRICS;

typedef struct {
    DWORD rgcx[4];
    DWORD fUpdateMaxWidths : 2;
} UAHMENUPOPUPMETRICS;

typedef struct {
    void* hmenu;
    HDC hdc;
    DWORD dwFlags;
} UAHMENU;

typedef struct {
    int iPosition;
    UAHMENUITEMMETRICS umim;
    UAHMENUPOPUPMETRICS umpm;
} UAHMENUITEM;

typedef struct {
    UINT CtlType;
    UINT CtlID;
    UINT itemID;
    UINT itemAction;
    UINT itemState;
    HWND hwndItem;
    HDC hdc;
    RECT rcItem;
    ULONG_PTR_T itemData;
} DRAWITEMSTRUCT;

typedef struct {
    DRAWITEMSTRUCT dis;
    UAHMENU um;
    UAHMENUITEM umi;
} UAHDRAWMENUITEM;

/* Win32 / function pointers */
typedef struct {
    DWORD cbSize;
    RECT rcBar;
    void* hMenu;
    HWND hwndMenu;
    BOOL fBarFocused:1;
    BOOL fFocused:1;
} MENUBARINFO;
typedef BOOL (*PFN_GetMenuBarInfo)(HWND, int, int, MENUBARINFO*);
typedef BOOL (*PFN_GetWindowRect2)(HWND, RECT*);
typedef int (*PFN_GetMenuStringW)(void*, UINT, wchar_t*, int, UINT);
typedef void* (*PFN_GetMenu)(HWND);
typedef int (*PFN_SetBkMode)(HDC, int);
typedef void* (*PFN_SetPropW)(HWND, const wchar_t*, void*);
typedef void* (*PFN_GetPropW)(HWND, const wchar_t*);
typedef BOOL (*PFN_GetClientRect)(HWND, RECT*);
typedef HDC (*PFN_GetDC_UAH)(HWND);
typedef int (*PFN_ReleaseDC_UAH)(HWND, HDC);

static PFN_GetMenuBarInfo p_GetMenuBarInfo;
static PFN_GetWindowRect2 p_GetWindowRect;
static PFN_GetMenuStringW p_GetMenuStringW;
static PFN_GetMenu p_GetMenu;
static PFN_SetBkMode p_SetBkMode_fn;
static PFN_SetPropW p_SetPropW;
static PFN_GetPropW p_GetPropW;
static PFN_GetClientRect p_GetClientRect;
static PFN_GetDC_UAH p_GetDC_uah;
static PFN_ReleaseDC_UAH p_ReleaseDC_uah;
static PFN_SetTextColor2 p_SetTextColor_uah;
static PFN_SetTextColor2 p_SetBkColor_uah;
static PFN_DrawTextW p_DrawTextW_uah;
static PFN_SelectObject p_SelectObject_uah;

static HBRUSH g_uah_bg_brush;       /* menu bar / item background */
static HBRUSH g_uah_sel_brush;      /* selected/hover item bg     */

static const wchar_t P_ORIG[] = L"uhooks4_uah_orig";
static const wchar_t P_MARK[] = L"uhooks4_uah_mark";

static LRESULT uah_wndproc(HWND, UINT, WPARAM, LPARAM);

static void paint_uah_menu(HWND h, HDC dc) {
    MENUBARINFO mbi;
    RECT wr;
    RECT bar;
    int k;
    if (!dc || !p_GetMenuBarInfo || !p_GetWindowRect || !p_FillRect || !g_uah_bg_brush) return;
    for (k = 0; k < (int)sizeof(mbi); ++k) ((unsigned char*)&mbi)[k] = 0;
    mbi.cbSize = (DWORD)sizeof(mbi);
    if (!p_GetMenuBarInfo(h, -3, 0, &mbi)) return;
    if (!p_GetWindowRect(h, &wr)) return;
    bar = mbi.rcBar;
    bar.left -= wr.left;
    bar.right -= wr.left;
    bar.top -= wr.top;
    bar.bottom -= wr.top;
    bar.top -= 1;
    p_FillRect(dc, &bar, g_uah_bg_brush);
}

static void paint_uah_menu_item(HWND h, UAHDRAWMENUITEM* u) {
    wchar_t label[128];
    UINT flags = 0x25u;  /* DT_CENTER | DT_VCENTER | DT_SINGLELINE */
    HBRUSH back;
    COLORREF fg;
    void* font = 0;
    void* oldfont = 0;
    int len;
    if (!u || !u->um.hdc || !p_GetMenuStringW || !p_FillRect) return;
    label[0] = 0;
    len = p_GetMenuStringW(u->um.hmenu, (UINT)u->umi.iPosition, label, 127, 0x400u);
    if (len < 0) len = 0;
    /* itemState: 0x01=selected, 0x40=hotlight, 0x80=inactive, 0x02=grayed */
    if (u->dis.itemState & 0x41u) back = g_uah_sel_brush;
    else back = g_uah_bg_brush;
    if (!back) back = g_uah_bg_brush;
    if (back) p_FillRect(u->um.hdc, &u->dis.rcItem, back);
    /* Text: always light so non-hovered items are visible (the bug was
       user32 drawing them with a dark color on dark bg). Disabled items
       get DC_GRAY. */
    if (u->dis.itemState & 0x86u) fg = DC_GRAY;
    else fg = CLR_TEXT;
    if (u->dis.itemState & 0x100u) flags |= 0x100000u; /* DT_HIDEPREFIX */
    if (p_SendMessageW && p_SelectObject_uah) {
        font = (void*)p_SendMessageW(h, 0x0031u, 0, 0); /* WM_GETFONT */
        if (font) oldfont = p_SelectObject_uah(u->um.hdc, font);
    }
    if (p_SetBkMode_fn) p_SetBkMode_fn(u->um.hdc, 1); /* TRANSPARENT */
    (void)textsink_raw_set_text_color(u->um.hdc, fg);
    (void)textsink_raw_draw_text(u->um.hdc, label, len, &u->dis.rcItem, flags);
    if (oldfont && p_SelectObject_uah) p_SelectObject_uah(u->um.hdc, oldfont);
}

static LRESULT uah_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    LONG_PTR orig = 0;
    if (p_GetPropW) orig = (LONG_PTR)p_GetPropW(h, P_ORIG);

    /* Window inset fill (dialog bg below menu bar / above status bar):
       painted via WM_ERASEBKGND with the window-class background brush
       (cached light BTNFACE from user32 class registration — invisible
       to every color hook). Handle it here: fill dark + return dark
       brush for WM_CTLCOLOR* (dark-p2's proven KIND_TOP pattern). */
    if (msg == 0x0014u) { /* WM_ERASEBKGND */
        RECT rc;
        if (p_GetClientRect && p_FillRect && g_uah_bg_brush) {
            p_GetClientRect(h, &rc);
            p_FillRect((HDC)wp, &rc, g_uah_bg_brush);
        }
        return 1;
    }
    if (msg == 0x0132u || /* WM_CTLCOLORMSGBOX   */
        msg == 0x0133u || /* WM_CTLCOLOREDIT     */
        msg == 0x0134u || /* WM_CTLCOLORLISTBOX  */
        msg == 0x0135u || /* WM_CTLCOLORBTN      */
        msg == 0x0136u || /* WM_CTLCOLORDLG      */
        msg == 0x0137u || /* WM_CTLCOLORSCROLLBAR*/
        msg == 0x0138u) { /* WM_CTLCOLORSTATIC   */
        if (p_SetTextColor_uah) p_SetTextColor_uah((HDC)wp, CLR_TEXT);
        if (p_SetBkColor_uah) p_SetBkColor_uah((HDC)wp, DC_BG);
        return (LRESULT)g_uah_bg_brush;
    }

    if (msg == 0x0091u) { /* UAH draw menu bar background */
        UAHMENU* u = (UAHMENU*)lp;
        if (u && p_GetMenu && u->hmenu == p_GetMenu(h)) {
            paint_uah_menu(h, u->hdc);
            return 0;
        }
    }
    if (msg == 0x0092u) { /* UAH draw menu item */
        UAHDRAWMENUITEM* u = (UAHDRAWMENUITEM*)lp;
        if (u && p_GetMenu && u->um.hmenu == p_GetMenu(h)) {
            paint_uah_menu_item(h, u);
            return 0;
        }
    }

    if (orig) return iat_CallWindowProcW(orig, h, msg, wp, lp);
    return 0;
}

static void uah_subclass(HWND h) {
    LONG_PTR old;
    if (!p_GetPropW || !p_SetPropW) return;
    if (p_GetPropW(h, P_MARK)) return;
    old = iat_SetWindowLongPtrW(h, -4, (LONG_PTR)&uah_wndproc);
    p_SetPropW(h, P_ORIG, (HANDLE)old);
    p_SetPropW(h, P_MARK, (HANDLE)1);
    if (iat_InvalidateRect) iat_InvalidateRect(h, 0, TRUE);
}

static BOOL uah_child_dialog_cb(HWND h, LPARAM lp) {
    wchar_t cls[32];
    (void)lp;
    cls[0] = 0;
    if (iat_GetClassNameW) iat_GetClassNameW(h, cls, 31);
    cls[31] = 0;
    if (eqw(cls, L"#32770")) uah_subclass(h);
    return TRUE;
}

void uahmenu_init(HMODULE u, HMODULE g) {
    p_GetMenuBarInfo = (PFN_GetMenuBarInfo)resolve(u, "GetMenuBarInfo");
    p_GetWindowRect = (PFN_GetWindowRect2)resolve(u, "GetWindowRect");
    p_GetMenuStringW = (PFN_GetMenuStringW)resolve(u, "GetMenuStringW");
    p_GetMenu = (PFN_GetMenu)resolve(u, "GetMenu");
    p_SetBkMode_fn = (PFN_SetBkMode)resolve(g, "SetBkMode");
    p_SetPropW = (PFN_SetPropW)resolve(u, "SetPropW");
    p_GetPropW = (PFN_GetPropW)resolve(u, "GetPropW");
    p_GetClientRect = (PFN_GetClientRect)resolve(u, "GetClientRect");
    p_GetDC_uah = (PFN_GetDC_UAH)resolve(u, "GetDC");
    p_ReleaseDC_uah = (PFN_ReleaseDC_UAH)resolve(u, "ReleaseDC");
    p_SetTextColor_uah = (PFN_SetTextColor2)resolve(g, "SetTextColor");
    p_SetBkColor_uah = (PFN_SetTextColor2)resolve(g, "SetBkColor");
    p_DrawTextW_uah = (PFN_DrawTextW)resolve(u, "DrawTextW");
    p_SelectObject_uah = (PFN_SelectObject)resolve(g, "SelectObject");
    if (p_CreateSolidBrush) {
        g_uah_bg_brush = p_CreateSolidBrush(DC_BG);
        g_uah_sel_brush = p_CreateSolidBrush(DC_MENUHILITE);
    }
}

void uahmenu_on_top_window(HWND h) {
    uah_subclass(h);
    /* Page dialogs are child #32770 windows; their WM_CTLCOLOREDIT/STATIC
       brushes are what color the Edit value fields on the Performance tab.
       Subclass them too so uah_wndproc returns the dark brush. */
    if (p_EnumChildWindows) p_EnumChildWindows(h, uah_child_dialog_cb, 0);
    graphedge_subclass_tree(h);
}
