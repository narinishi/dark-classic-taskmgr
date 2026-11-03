#include "common.h"

/* ==================================================================
   DARK GRAPH / VALUE-FIELD BORDERS

   The graph controls (and their numeric value overlays) are owner-drawn
   Buttons: style low nibble 0xB (BS_OWNERDRAW), ex-style 0x200
   (WS_EX_CLIENTEDGE). The light 3D outline around each graph is
   user32/win32k's classic client-edge painting of that NC band; it does
   not go through theme records or the hooked GetSysColor path.

   We subclass exactly those buttons and handle WM_NCPAINT ourselves:
   fill the non-client band with the dark background and draw a 1px
   subtle edge. The owner-drawn client content is untouched.

   Flicker: the button and its enclosing graph frame erase with light
   brushes on graph ticks. The host frame is the DavesFrameClass sibling
   that contains the button. We darken WM_ERASEBKGND for both and drop
   the frame's redundant identical WM_SETTEXT (which would clear/redraw
   the caption).
   Gate: GRAPH_EDGE_DARK.
   ================================================================== */

typedef void* (*PFN_GetPropW_GB)(HWND, const wchar_t*);
typedef void* (*PFN_SetPropW_GB)(HWND, const wchar_t*, void*);
typedef HDC (*PFN_GetWindowDC_GB)(HWND);
typedef int (*PFN_ReleaseDC_GB)(HWND, HDC);
typedef BOOL (*PFN_GetWindowRect_GB)(HWND, RECT*);
typedef BOOL (*PFN_EnumChildWindows_GB)(HWND, BOOL (*)(HWND, LPARAM), LPARAM);
typedef HWND (*PFN_GetParent_GB)(HWND);
typedef BOOL (*PFN_GetClientRect_GB)(HWND, RECT*);
typedef int (*PFN_GetWindowTextW_GB)(HWND, wchar_t*, int);

static PFN_GetPropW_GB p_gb_getprop;
static PFN_SetPropW_GB p_gb_setprop;
static PFN_GetWindowDC_GB p_gb_getdc;
static PFN_ReleaseDC_GB p_gb_releasedc;
static PFN_GetWindowRect_GB p_gb_getrect;
static PFN_EnumChildWindows_GB p_gb_enum;
static PFN_GetParent_GB p_gb_getparent;
static PFN_GetClientRect_GB p_gb_getclient;
static PFN_GetWindowTextW_GB p_gb_gettext;
static HBRUSH g_gb_fill;
static HBRUSH g_gb_edge;

static const wchar_t P_GB_ORIG[] = L"uhooks4_gb_orig";
static const wchar_t P_GB_MARK[] = L"uhooks4_gb_mark";
static const wchar_t P_HOST_ORIG[] = L"uhooks4_gb_host_orig";
static const wchar_t P_HOST_MARK[] = L"uhooks4_gb_host_mark";

#define WM_NCPAINT_ 0x0085u
#define WM_ERASEBKGND_ 0x0014u
#define WS_EX_CLIENTEDGE_ 0x00000200L
#define BS_TYPEMASK_ 0x0000000FL
#define BS_OWNERDRAW_ 0x0000000BL

static int gb_fill_client(HWND h, HDC dc) {
    RECT rc;
    if (!dc || !g_gb_fill || !p_FillRect || !p_gb_getclient) return 0;
    if (!p_gb_getclient(h, &rc)) return 0;
    p_FillRect(dc, &rc, g_gb_fill);
    return 1;
}

/* Container of a graph button (no class-name matching): darken its erase
   so graph ticks do not flash a light background behind the captions. */
static LRESULT hostdark_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    LONG_PTR orig = p_gb_getprop ? (LONG_PTR)p_gb_getprop(h, P_HOST_ORIG) : 0;
    /* The app re-sets the interface-name caption every tick. Forwarding an
       identical WM_SETTEXT clears and redraws the caption, which is the
       visible flicker. Drop it when the text is unchanged. */
    if (msg == 0x000Cu && lp && p_gb_gettext) {
        wchar_t cur[256];
        cur[0] = 0;
        p_gb_gettext(h, cur, 255);
        cur[255] = 0;
        if (eqw(cur, (const wchar_t*)lp)) return 1;
    }
    if (msg == WM_ERASEBKGND_ && wp && gb_fill_client(h, (HDC)wp)) return 1;
    return orig ? iat_CallWindowProcW(orig, h, msg, wp, lp) : 0;
}

static void hostdark_subclass(HWND h) {
    LONG_PTR old;
    if (!h || !p_gb_getprop || !p_gb_setprop) return;
    if (p_gb_getprop(h, P_HOST_MARK)) return;
    old = iat_SetWindowLongPtrW(h, -4, (LONG_PTR)&hostdark_proc);
    p_gb_setprop(h, P_HOST_ORIG, (HANDLE)old);
    p_gb_setprop(h, P_HOST_MARK, (HANDLE)1);
}

static LRESULT gb_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    LONG_PTR orig = p_gb_getprop ? (LONG_PTR)p_gb_getprop(h, P_GB_ORIG) : 0;
    if (msg == WM_ERASEBKGND_ && wp && gb_fill_client(h, (HDC)wp)) return 1;
    if (msg == WM_NCPAINT_ && g_gb_fill) {
        HDC dc = p_gb_getdc ? p_gb_getdc(h) : 0;
        if (dc) {
            RECT wr, rc;
            if (p_gb_getrect && p_gb_getrect(h, &wr)) {
                rc.left = 0; rc.top = 0;
                rc.right = wr.right - wr.left;
                rc.bottom = wr.bottom - wr.top;
                p_FillRect(dc, &rc, g_gb_fill);
                if (p_FrameRect && g_gb_edge) p_FrameRect(dc, &rc, g_gb_edge);
            }
            if (p_gb_releasedc) p_gb_releasedc(h, dc);
            return 0;
        }
    }
    return orig ? iat_CallWindowProcW(orig, h, msg, wp, lp) : 0;
}

typedef struct { HWND btn; RECT br; HWND found; } GB_FIND;

static BOOL gb_find_cb(HWND h, LPARAM lp) {
    GB_FIND* f = (GB_FIND*)lp;
    RECT r;
    wchar_t cls[32];
    if (!f || h == f->btn || !p_gb_getrect) return TRUE;
    cls[0] = 0;
    if (iat_GetClassNameW) iat_GetClassNameW(h, cls, 31);
    cls[31] = 0;
    if (!eqw(cls, L"DavesFrameClass")) return TRUE;
    if (!p_gb_getrect(h, &r)) return TRUE;
    if (r.left <= f->br.left && r.top <= f->br.top &&
        r.right >= f->br.right && r.bottom >= f->br.bottom)
        f->found = h;
    return TRUE;
}

static void gb_subclass(HWND h) {
    wchar_t cls[32];
    LONG_PTR st, ex, old;
    if (!p_gb_getprop || !p_gb_setprop || !p_GetWindowLongPtrW) return;
    if (p_gb_getprop(h, P_GB_MARK)) return;
    cls[0] = 0;
    if (iat_GetClassNameW) iat_GetClassNameW(h, cls, 31);
    cls[31] = 0;
    if (!eqw(cls, L"Button")) return;
    st = p_GetWindowLongPtrW(h, -16);
    ex = p_GetWindowLongPtrW(h, -20);
    if ((st & BS_TYPEMASK_) != BS_OWNERDRAW_) return;
    if (!(ex & WS_EX_CLIENTEDGE_)) return;
    old = iat_SetWindowLongPtrW(h, -4, (LONG_PTR)&gb_proc);
    p_gb_setprop(h, P_GB_ORIG, (HANDLE)old);
    p_gb_setprop(h, P_GB_MARK, (HANDLE)1);
    if (iat_InvalidateRect) iat_InvalidateRect(h, 0, TRUE);
    /* The button may already have painted its light WS_EX_CLIENTEDGE
       band before this subclass landed (lazily created page). Invalidate
       the *frame*, not just the client, so WM_NCPAINT is repainted
       through gb_proc now. */
    if (p_RedrawWindow)
        p_RedrawWindow(h, 0, 0, 0x0001u /*INVALIDATE*/ | 0x0400u /*FRAME*/ | 0x0100u /*UPDATENOW*/);
    /* Darken the erase of the enclosing DavesFrameClass graph frame. */
    if (p_gb_getparent && p_gb_enum && p_gb_getrect) {
        HWND par = p_gb_getparent(h);
        GB_FIND f;
        f.btn = h;
        f.found = 0;
        if (par && p_gb_getrect(h, &f.br)) {
            p_gb_enum(par, gb_find_cb, (LPARAM)&f);
            if (f.found) hostdark_subclass(f.found);
        }
    }
}

static BOOL gb_cb(HWND h, LPARAM lp) {
    (void)lp;
    gb_subclass(h);
    return TRUE;
}

void graphedge_init(HMODULE u, HMODULE g) {
    p_gb_getprop = (PFN_GetPropW_GB)resolve(u, "GetPropW");
    p_gb_setprop = (PFN_SetPropW_GB)resolve(u, "SetPropW");
    p_gb_getdc = (PFN_GetWindowDC_GB)resolve(u, "GetWindowDC");
    p_gb_releasedc = (PFN_ReleaseDC_GB)resolve(u, "ReleaseDC");
    p_gb_getrect = (PFN_GetWindowRect_GB)resolve(u, "GetWindowRect");
    p_gb_enum = (PFN_EnumChildWindows_GB)resolve(u, "EnumChildWindows");
    p_gb_getparent = (PFN_GetParent_GB)resolve(u, "GetParent");
    p_gb_getclient = (PFN_GetClientRect_GB)resolve(u, "GetClientRect");
    p_gb_gettext = (PFN_GetWindowTextW_GB)resolve(u, "GetWindowTextW");
    if (p_CreateSolidBrush) {
        g_gb_fill = p_CreateSolidBrush(DC_BG);
        g_gb_edge = p_CreateSolidBrush(DC_VIS_EDGE);
    }
    (void)g;
}

void graphedge_subclass_tree(HWND root) {
    if (!root || !p_gb_enum) return;
    gb_subclass(root);
    p_gb_enum(root, gb_cb, 0);
}
