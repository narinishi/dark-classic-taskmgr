#include "common.h"

/* ==================================================================
   T: TEXT SINK HOOKS — the true text chokepoints.

   The draw hooks force light text on the dark classic UI. DirectUI
   TaskDialog content is drawn on window-less memory DCs, and its
   button text is drawn on the dialog #32770 DC; those are native-light
   surfaces, so DUI paint/root checks bypass the forcing. See dui.c.
   ================================================================== */
typedef BOOL (*PFN_ExtTextOutW)(HDC, int, int, UINT, const RECT*, const wchar_t*, UINT, const int*);
typedef BOOL (*PFN_TextOutW)(HDC, int, int, const wchar_t*, UINT);
typedef int (*PFN_DrawTextW)(HDC, const wchar_t*, int, void*, UINT);
typedef int (*PFN_DrawTextExW)(HDC, const wchar_t*, int, void*, UINT, void*);
typedef COLORREF (*PFN_GetTextColor)(HDC);

static PFN_SetTextColor2 p_real_SetTextColor;
static PFN_GetTextColor p_real_GetTextColor;
static PFN_ExtTextOutW p_real_ExtTextOutW;
static PFN_TextOutW p_real_TextOutW;
static PFN_DrawTextW p_real_DrawTextW;
static PFN_DrawTextExW p_real_DrawTextExW;

/* Classic disabled text tone (COLOR_GRAYTEXT, ~0x6D) used by menus and
   other disabled controls. Keep it instead of forcing light, otherwise
   disabled items look identical to enabled ones. */
static int is_disabled_gray(COLORREF c) {
    int r = (int)(c & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)((c >> 16) & 0xFF);
    int sat = abs_int(r - g) + abs_int(g - b) + abs_int(r - b);
    int lum = lum_of(c);
    return sat <= 40 && lum >= 80 && lum <= 130;
}

static void force_light_text(HDC dc) {
    if (dui_should_bypass(dc)) return;
    if (p_real_GetTextColor) {
        COLORREF cur = p_real_GetTextColor(dc);
        if (is_disabled_gray(cur)) return;   /* preserve disabled styling */
    }
    if (p_real_SetTextColor) p_real_SetTextColor(dc, CLR_TEXT);
}

BOOL hook_ExtTextOutW(HDC dc, int x, int y, UINT opts, const RECT* rc,
                      const wchar_t* s, UINT n, const int* dx) {
    if (!on_ui_thread())
        return p_real_ExtTextOutW ? p_real_ExtTextOutW(dc, x, y, opts, rc, s, n, dx) : 0;
    force_light_text(dc);
    return p_real_ExtTextOutW ? p_real_ExtTextOutW(dc, x, y, opts, rc, s, n, dx) : 0;
}

BOOL hook_TextOutW(HDC dc, int x, int y, const wchar_t* s, UINT n) {
    if (!on_ui_thread())
        return p_real_TextOutW ? p_real_TextOutW(dc, x, y, s, n) : 0;
    force_light_text(dc);
    return p_real_TextOutW ? p_real_TextOutW(dc, x, y, s, n) : 0;
}

int hook_DrawTextW(HDC dc, const wchar_t* s, int n, void* rc, UINT fmt) {
    if (!on_ui_thread())
        return p_real_DrawTextW ? p_real_DrawTextW(dc, s, n, rc, fmt) : 0;
    force_light_text(dc);
    return p_real_DrawTextW ? p_real_DrawTextW(dc, s, n, rc, fmt) : 0;
}

int hook_DrawTextExW(HDC dc, const wchar_t* s, int n, void* rc, UINT fmt, void* params) {
    if (!on_ui_thread())
        return p_real_DrawTextExW ? p_real_DrawTextExW(dc, s, n, rc, fmt, params) : 0;
    force_light_text(dc);
    return p_real_DrawTextExW ? p_real_DrawTextExW(dc, s, n, rc, fmt, params) : 0;
}

void textsink_init(HMODULE g, HMODULE u) {
    p_real_SetTextColor = (PFN_SetTextColor2)resolve(g, "SetTextColor");
    p_real_GetTextColor = (PFN_GetTextColor)resolve(g, "GetTextColor");
    p_real_ExtTextOutW = (PFN_ExtTextOutW)resolve(g, "ExtTextOutW");
    p_real_TextOutW = (PFN_TextOutW)resolve(g, "TextOutW");
    p_real_DrawTextW = (PFN_DrawTextW)resolve(u, "DrawTextW");
    p_real_DrawTextExW = (PFN_DrawTextExW)resolve(u, "DrawTextExW");
}

void textsink_attach(void) {
    if (p_real_ExtTextOutW)
        hook_attach((void**)&p_real_ExtTextOutW, (void*)&hook_ExtTextOutW);
    if (p_real_TextOutW)
        hook_attach((void**)&p_real_TextOutW, (void*)&hook_TextOutW);
    if (p_real_DrawTextW)
        hook_attach((void**)&p_real_DrawTextW, (void*)&hook_DrawTextW);
    if (p_real_DrawTextExW)
        hook_attach((void**)&p_real_DrawTextExW, (void*)&hook_DrawTextExW);
}

/* Raw (pre-detour) entry points for callers that intentionally choose a
   color and must not be overridden by the light-text policy (uahmenu). */
COLORREF textsink_raw_set_text_color(HDC dc, COLORREF c) {
    return p_real_SetTextColor ? p_real_SetTextColor(dc, c) : c;
}

int textsink_raw_draw_text(HDC dc, const wchar_t* s, int n, void* rc, UINT fmt) {
    return p_real_DrawTextW ? p_real_DrawTextW(dc, s, n, rc, fmt) : 0;
}
