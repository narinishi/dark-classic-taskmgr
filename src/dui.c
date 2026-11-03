#include "common.h"

#define DUI_TH_MAX 32
#define GA_ROOT_ 2u

typedef void* (*PFN_OTD)(HWND, const wchar_t*);
typedef void* (*PFN_OTDD)(HWND, const wchar_t*, UINT);
typedef long (*PFN_CTD)(void*);
typedef HWND (*PFN_GetAncestor_D)(HWND, UINT);
typedef void* (*PFN_GetProp_D)(HWND, const wchar_t*);
typedef void* (*PFN_SetProp_D)(HWND, const wchar_t*, void*);
typedef HWND (*PFN_GetForegroundWindow_D)(void);

static PFN_OTD p_real_otd;
static PFN_OTDD p_real_otdd;
static PFN_CTD p_real_ctd;
static PFN_GetAncestor_D p_getancestor;
static PFN_GetProp_D p_getprop;
static PFN_SetProp_D p_setprop;
static PFN_WindowFromDC p_wfdc;
static PFN_GetForegroundWindow_D p_getforeground;

static void* g_th[DUI_TH_MAX];
static unsigned char g_th_native[DUI_TH_MAX];
static unsigned int g_th_next;

static int g_dui_depth;

static const wchar_t P_ORIG[] = L"uhooks5_dui_orig";
static const wchar_t P_MARK[] = L"uhooks5_dui_mark";
static const wchar_t P_ROOT[] = L"uhooks5_dui_root";

static int is_dui_class(const wchar_t* cls) {
    return cls && (eqw(cls, L"TaskDialogStyle") ||
                   eqw(cls, L"TEXTSTYLE") ||
                   eqw(cls, L"CONTROLPANELSTYLE"));
}

static void theme_mark(void* th, int native) {
    unsigned int i;
    if (!th) return;
    for (i = 0; i < DUI_TH_MAX; ++i) {
        if (g_th[i] == th) {
            g_th_native[i] = (unsigned char)native;
            return;
        }
    }
    i = g_th_next++ % DUI_TH_MAX;
    g_th[i] = th;
    g_th_native[i] = (unsigned char)native;
}

int theme_is_directui(void* th) {
    unsigned int i;
    if (!th) return 0;
    for (i = 0; i < DUI_TH_MAX; ++i)
        if (g_th[i] == th) return g_th_native[i] != 0;
    return 0;
}

static void* hook_OTD(HWND h, const wchar_t* cls) {
    void* th = p_real_otd ? p_real_otd(h, cls) : 0;
    theme_mark(th, is_dui_class(cls));
    return th;
}

static void* hook_OTDD(HWND h, const wchar_t* cls, UINT dpi) {
    void* th = p_real_otdd ? p_real_otdd(h, cls, dpi) : 0;
    theme_mark(th, is_dui_class(cls));
    return th;
}

static long hook_CTD(void* th) {
    long r = p_real_ctd ? p_real_ctd(th) : -1;
    theme_mark(th, 0);
    return r;
}

static LRESULT dui_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    LONG_PTR orig = p_getprop ? (LONG_PTR)p_getprop(h, P_ORIG) : 0;
    LRESULT r;
    g_dui_depth++;
    r = orig ? iat_CallWindowProcW(orig, h, msg, wp, lp) : 0;
    g_dui_depth--;
    return r;
}

void dui_on_window(HWND h) {
    wchar_t cls[32];
    LONG_PTR old;
    HWND root;
    if (!h || !p_getprop || !p_setprop || !iat_GetClassNameW) return;
    cls[0] = 0;
    iat_GetClassNameW(h, cls, 31);
    cls[31] = 0;
    if (!eqw(cls, L"DirectUIHWND")) return;
    if (p_getprop(h, P_MARK)) return;
    old = iat_SetWindowLongPtrW(h, -4, (LONG_PTR)&dui_proc);
    p_setprop(h, P_ORIG, (HANDLE)old);
    p_setprop(h, P_MARK, (HANDLE)1);
    root = p_getancestor ? p_getancestor(h, GA_ROOT_) : 0;
    if (root) {
        p_setprop(root, P_ROOT, (HANDLE)1);
        if (p_RedrawWindow)
            p_RedrawWindow(root, 0, 0, 0x1u | 0x4u | 0x80u | 0x100u);
    } else if (p_RedrawWindow) {
        p_RedrawWindow(h, 0, 0, 0x1u | 0x4u | 0x80u | 0x100u);
    }
}

int dui_should_bypass(HDC dc) {
    HWND w;
    HWND root;
    if (g_dui_depth > 0) return 1;
    if (!p_wfdc || !dc) return 0;
    w = p_wfdc(dc);
    if (!w) w = p_getforeground ? p_getforeground() : 0;
    if (!w) return 0;
    root = p_getancestor ? p_getancestor(w, GA_ROOT_) : 0;
    return (root && p_getprop && p_getprop(root, P_ROOT)) ? 1 : 0;
}

void dui_init(void) {
    HMODULE ux = iat_GetModuleHandleW(S_UXTHEME);
    HMODULE u = iat_GetModuleHandleW(S_USER32);
    p_real_otd = (PFN_OTD)resolve(ux, "OpenThemeData");
    p_real_otdd = (PFN_OTDD)resolve(ux, "OpenThemeDataForDpi");
    p_real_ctd = (PFN_CTD)resolve(ux, "CloseThemeData");
    p_getancestor = (PFN_GetAncestor_D)resolve(u, "GetAncestor");
    p_getprop = (PFN_GetProp_D)resolve(u, "GetPropW");
    p_setprop = (PFN_SetProp_D)resolve(u, "SetPropW");
    p_wfdc = (PFN_WindowFromDC)resolve(u, "WindowFromDC");
    p_getforeground = (PFN_GetForegroundWindow_D)resolve(u, "GetForegroundWindow");
}

void dui_attach(void) {
    if (p_real_otd)
        hook_attach((void**)&p_real_otd, (void*)&hook_OTD);
    if (p_real_otdd)
        hook_attach((void**)&p_real_otdd, (void*)&hook_OTDD);
    if (p_real_ctd)
        hook_attach((void**)&p_real_ctd, (void*)&hook_CTD);
}
