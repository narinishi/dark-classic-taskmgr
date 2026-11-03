#include "common.h"

/* ==================================================================
   DARK TITLE BAR

   The caption/non-client frame is DWM-rendered, so GDI/theme/syscolor
   hooks cannot recolour it. Windows exposes a per-window opt-in:

     DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &TRUE, 4)

   DWMWA_USE_IMMERSIVE_DARK_MODE is 20 on current Windows 11 builds and
   19 on older Windows 10 builds; we try 20 then 19. Applied once per
   top-level window (property-marked) and followed by a frame redraw.
   Gate: TITLEBAR_DARK.
   ================================================================== */

typedef long (*PFN_DwmSetWindowAttribute)(HWND, DWORD, const void*, DWORD);
typedef void* (*PFN_GetPropW_TB)(HWND, const wchar_t*);
typedef void* (*PFN_SetPropW_TB)(HWND, const wchar_t*, void*);

static PFN_DwmSetWindowAttribute p_tb_dwm_set;
static PFN_GetPropW_TB p_tb_getprop;
static PFN_SetPropW_TB p_tb_setprop;

static const wchar_t P_TB_MARK[] = L"uhooks4_titlebar_mark";
static const wchar_t S_DWMAPI[] = L"dwmapi.dll";

#define DWMWA_USE_IMMERSIVE_DARK_MODE_     20u
#define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD_ 19u

static void titlebar_apply(HWND h) {
    BOOL on = 1;
    long hr;
    if (!h || !p_tb_dwm_set) return;
    if (p_tb_getprop && p_tb_getprop(h, P_TB_MARK)) return;
    hr = p_tb_dwm_set(h, DWMWA_USE_IMMERSIVE_DARK_MODE_, &on, (DWORD)sizeof(on));
    if (hr < 0)
        hr = p_tb_dwm_set(h, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD_, &on, (DWORD)sizeof(on));
    if (hr < 0) return;
    if (p_tb_setprop) p_tb_setprop(h, P_TB_MARK, (HANDLE)1);
    if (p_RedrawWindow) p_RedrawWindow(h, 0, 0, 0x1u /*INVALIDATE*/ | 0x400u /*FRAME*/);
}

void titlebar_init(HMODULE u) {
    HMODULE dwm = iat_LoadLibraryW(S_DWMAPI);
    p_tb_dwm_set = dwm ? (PFN_DwmSetWindowAttribute)resolve(dwm, "DwmSetWindowAttribute") : 0;
    p_tb_getprop = (PFN_GetPropW_TB)resolve(u, "GetPropW");
    p_tb_setprop = (PFN_SetPropW_TB)resolve(u, "SetPropW");
}

void titlebar_apply_window(HWND h) {
    titlebar_apply(h);
}
