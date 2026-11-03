#include "common.h"

static const wchar_t S_DARK_EXPLORER[] = L"DarkMode_Explorer";
static const wchar_t S_DARK_ITEMSVIEW[] = L"DarkMode_ItemsView";

/* ==================================================================
   LIST HEADER SEAM

   The native body separator already continues at header-item-right - 1;
   the header theme paints its divider at header-item-right. Shift the
   header's native divider pixels left by one pixel after each paint,
   preserving the original anti-aliased colors. The ListView body and its
   column geometry are left untouched.
   ================================================================== */
typedef void* (*PFN_GetPropW_HD)(HWND, const wchar_t*);
typedef void* (*PFN_SetPropW_HD)(HWND, const wchar_t*, void*);
typedef BOOL (*PFN_GetClientRect_HD)(HWND, RECT*);
typedef HDC (*PFN_GetDC_HD)(HWND);
typedef int (*PFN_ReleaseDC_HD)(HWND, HDC);
typedef BOOL (*PFN_BitBlt_HD)(HDC, int, int, int, int, HDC, int, int, DWORD);

static PFN_GetPropW_HD p_hd_getprop;
static PFN_SetPropW_HD p_hd_setprop;
static PFN_GetClientRect_HD p_hd_getclient;
static PFN_GetDC_HD p_hd_getdc;
static PFN_ReleaseDC_HD p_hd_releasedc;
static PFN_BitBlt_HD p_hd_bitblt;

static const wchar_t P_HDR_ORIG[] = L"uhooks5_hdr_orig";
static const wchar_t P_HDR_MARK[] = L"uhooks5_hdr_mark";

#define HDM_GETITEMCOUNT_ 0x1200u
#define HDM_GETITEMRECT_ 0x1207u
#define WM_PAINT_ 0x000Fu
#define WM_PRINT_ 0x0317u
#define WM_PRINTCLIENT_ 0x0318u
#define PRF_CLIENT_ 0x00000004u
#define SRCCOPY_ 0x00CC0020u

static int g_in_seam;

static void shift_header_seams(HWND h, HDC dc) {
    RECT client;
    int count, i;

    if (!dc || !p_hd_getclient || !p_hd_bitblt || !p_SendMessageW)
        return;
    if (!p_hd_getclient(h, &client)) return;

    count = (int)p_SendMessageW(h, HDM_GETITEMCOUNT_, 0, 0);
    for (i = 0; i < count; ++i) {
        RECT r;
        int x, top, height;

        if (!p_SendMessageW(h, HDM_GETITEMRECT_, (WPARAM)i, (LPARAM)&r))
            continue;

        x = r.right;
        top = r.top;
        height = r.bottom - r.top;
        if (x < 2 || x + 1 >= client.right || height <= 0)
            continue;

        /* Move the native anti-aliased divider group left one pixel:
           x-1 -> x-2, x -> x-1, and clear old x from x+1. */
        p_hd_bitblt(dc, x - 2, top, 1, height, dc, x - 1, top, SRCCOPY_);
        p_hd_bitblt(dc, x - 1, top, 1, height, dc, x, top, SRCCOPY_);
        p_hd_bitblt(dc, x, top, 1, height, dc, x + 1, top, SRCCOPY_);
    }
}

static LRESULT hdr_seam_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    LONG_PTR orig = p_hd_getprop ? (LONG_PTR)p_hd_getprop(h, P_HDR_ORIG) : 0;
    LRESULT result = orig ? iat_CallWindowProcW(orig, h, msg, wp, lp) : 0;

    if (g_in_seam) return result;

    if (msg == WM_PAINT_) {
        HDC dc = p_hd_getdc ? p_hd_getdc(h) : 0;
        if (dc) {
            g_in_seam = 1;
            shift_header_seams(h, dc);
            g_in_seam = 0;
            if (p_hd_releasedc) p_hd_releasedc(h, dc);
        }
    } else if ((msg == WM_PRINT_ && (lp & PRF_CLIENT_)) ||
               msg == WM_PRINTCLIENT_) {
        g_in_seam = 1;
        shift_header_seams(h, (HDC)wp);
        g_in_seam = 0;
    }

    return result;
}

static void header_seam_install(HWND h) {
    wchar_t cls[32];
    LONG_PTR old;

    if (!h || !p_hd_getprop || !p_hd_setprop || !iat_GetClassNameW)
        return;
    if (p_hd_getprop(h, P_HDR_MARK)) return;

    cls[0] = 0;
    iat_GetClassNameW(h, cls, 31);
    cls[31] = 0;
    if (!eqw(cls, L"SysHeader32")) return;

    old = iat_SetWindowLongPtrW(h, -4, (LONG_PTR)&hdr_seam_proc);
    p_hd_setprop(h, P_HDR_ORIG, (HANDLE)old);
    p_hd_setprop(h, P_HDR_MARK, (HANDLE)1);
    if (iat_InvalidateRect) iat_InvalidateRect(h, 0, TRUE);
}

void apply_swtheme(HWND h) {
    wchar_t cls[64];
    if (!p_SetWindowTheme || !iat_GetClassNameW) return;
    cls[0] = 0;
    iat_GetClassNameW(h, cls, 63);
    cls[63] = 0;
    if (eqw(cls, L"SysListView32")) {
        p_SetWindowTheme(h, S_DARK_EXPLORER, 0);
        return;
    }
    if (eqw(cls, L"SysHeader32") || eqw(cls, L"ToolbarWindow32") ||
        eqw(cls, L"SysTabControl32") || eqw(cls, L"SysListBox32")) {
        p_SetWindowTheme(h, S_DARK_ITEMSVIEW, 0);
        if (eqw(cls, L"SysHeader32"))
            header_seam_install(h);
        return;
    }
    if (eqw(cls, L"Button") || eqw(cls, L"Edit") ||
        eqw(cls, L"ComboBox") || eqw(cls, L"msctls_statusbar32") ||
        eqw(cls, L"msctls_progress32") || eqw(cls, L"ReBarWindow32") ||
        eqw(cls, L"msctls_trackbar32") || eqw(cls, L"SysTreeView32") ||
        eqw(cls, L"ScrollBar") ||
        eqw(cls, L"tooltips_class32")) {
        p_SetWindowTheme(h, S_DARK_EXPLORER, 0);
    }
}

typedef void* (*PFN_GetMenuW)(HWND);

static BOOL child_cb(HWND h, LPARAM lp) { (void)lp; apply_swtheme(h); return TRUE; }
static int g_busy;
static PFN_GetWindowThreadProcessId p_gwtpid;
static PFN_GetMenuW p_getmenu;

void on_event(HANDLE hook, DWORD event, HWND h, long idObject, long idChild, DWORD thread, DWORD time) {
    LONG_PTR style;
    DWORD wtid = 0;
    (void)hook; (void)event; (void)idChild; (void)thread; (void)time;
    if (!h || idObject != 0) return;
    style = p_GetWindowLongPtrW ? p_GetWindowLongPtrW(h, -16) : 0;
    if (p_gwtpid) wtid = p_gwtpid(h, 0);
    if (!(style & 0x40000000) && p_getmenu && p_getmenu(h)) {
        /* Main Task Manager window: its owning thread is the UI thread. */
        if (wtid && wtid != g_ui_tid) {
            g_ui_tid = wtid;
            if (p_RedrawWindow)
                p_RedrawWindow(h, 0, 0, 0x1u | 0x4u | 0x80u | 0x400u | 0x100u);
        }
    } else if (wtid && g_ui_tid && wtid != g_ui_tid) {
        return;
    }
    if (g_busy) return;
    g_busy = 1;
    dui_on_window(h);
    /* Pages/controls can be created lazily (e.g. the Networking tab on
       first show), after the one-shot pass from the top-level event, so
       rescan this subtree on every window event. */
    graphedge_subclass_tree(h);
    {
        if (!(style & 0x40000000)) {
            apply_swtheme(h);
            uahmenu_on_top_window(h);
            titlebar_apply_window(h);
            if (p_EnumChildWindows) p_EnumChildWindows(h, child_cb, 0);
            if (p_SendMessageW) p_SendMessageW(h, 0x031Au, 0, 0);
            if (p_RedrawWindow) p_RedrawWindow(h, 0, 0, 0x1u | 0x4u | 0x80u | 0x400u);
        } else {
            apply_swtheme(h);
        }
    }
    g_busy = 0;
}

void swtheme_init(HMODULE ux) {
    int (*sp)(int);
    void (*refresh)(void);
    void (*flush)(void);
    HMODULE u = iat_GetModuleHandleW(S_USER32);
    HMODULE g = iat_GetModuleHandleW(S_GDI32);
    p_gwtpid = (PFN_GetWindowThreadProcessId)resolve(u, "GetWindowThreadProcessId");
    p_getmenu = (PFN_GetMenuW)resolve(u, "GetMenu");
    p_hd_getprop = (PFN_GetPropW_HD)resolve(u, "GetPropW");
    p_hd_setprop = (PFN_SetPropW_HD)resolve(u, "SetPropW");
    p_hd_getclient = (PFN_GetClientRect_HD)resolve(u, "GetClientRect");
    p_hd_getdc = (PFN_GetDC_HD)resolve(u, "GetDC");
    p_hd_releasedc = (PFN_ReleaseDC_HD)resolve(u, "ReleaseDC");
    p_hd_bitblt = (PFN_BitBlt_HD)resolve(g, "BitBlt");
    p_SetWindowTheme = (PFN_SetWindowTheme)resolve(ux, "SetWindowTheme");
    sp = (int (*)(int))resolve(ux, (const char*)135);
    refresh = (void (*)(void))resolve(ux, (const char*)104);
    flush = (void (*)(void))resolve(ux, (const char*)136);
    if (sp) sp(2);
    if (refresh) refresh();
    if (flush) flush();
    if (p_SetWinEventHook) {
        DWORD pid = iat_GetCurrentProcessId();
        p_SetWinEventHook(0x8000u, 0x8002u, 0, (void*)&on_event, pid, 0, 0);
    }
}
