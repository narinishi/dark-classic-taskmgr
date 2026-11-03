#include "common.h"

/* ==================================================================
   UHOOKS4: GetThemeColor EXPORT REMAP + PASS-THROUGH draw.
   (evolved from the former uhooks3-policy-b)

   No record rewriting. The painter decides what to draw (geometry
   preserved); we only decide what colors it sees (GetThemeColor).

   Auto-discovered complement: the three elements whose backgrounds are
   painted by the native theme painter without a GetThemeColor lookup
   (tab items, status bar, Edit value fields) get a narrow
   DrawThemeBackground fill. Only these classes are touched; every other
   draw falls through to the real painter, so button bevels, header
   grooves and list separators stay native. Gate: FURTHER_DARK_DTB.
   ================================================================== */

#define TMT_BORDERCOLOR 3801
#define TMT_FILLCOLOR   3802
#define TMT_TEXTCOLOR   3803

static PFN_GetThemeColor p_real_GetThemeColor;

int hook_GetThemeColor(void* theme, int part, int state, int prop, void* out) {
    int r;
    if (!on_ui_thread())
        return p_real_GetThemeColor ? p_real_GetThemeColor(theme, part, state, prop, out) : (int)0x80004001;
    if (theme_is_directui(theme))
        return p_real_GetThemeColor ? p_real_GetThemeColor(theme, part, state, prop, out) : (int)0x80004001;
    r = p_real_GetThemeColor ? p_real_GetThemeColor(theme, part, state, prop, out) : (int)0x80004001;
    if (out) {
        if (prop == TMT_TEXTCOLOR) {
            *(unsigned int*)out = (unsigned int)CLR_TEXT;
            if (r < 0) r = 0;
        } else if (prop == TMT_FILLCOLOR) {
            *(unsigned int*)out = (unsigned int)CLR_BG;
            if (r < 0) r = 0;
        } else if (prop == TMT_BORDERCOLOR) {
            *(unsigned int*)out = (unsigned int)DC_EDGE;
            if (r < 0) r = 0;
        }
    }
    return r;
}

/* Elements whose background the native painter fills itself (no
   GetThemeColor lookup): tab items and the status bar. Fill only these
   classes; pass every other theme draw through untouched. (Edit value
   fields are handled in uahmenu.c via the page-dialog CTLCOLOR brush —
   their paint path never reaches DrawThemeBackground here.) */
static PFN_DrawThemeBackground p_real_DrawThemeBackground;
static PFN_WindowFromDC p_fd_wfdc;
static HBRUSH g_fd_fill;
static HBRUSH g_fd_tab_inactive;
static HBRUSH g_fd_edge;
static RECT g_fd_pane;
static int g_fd_pane_valid;

static int fd_class_is(HDC dc, const wchar_t* want) {
    wchar_t cls[32];
    HWND w;
    if (!p_fd_wfdc || !iat_GetClassNameW) return 0;
    w = p_fd_wfdc(dc);
    if (!w) return 0;
    cls[0] = 0;
    iat_GetClassNameW(w, cls, 31);
    cls[31] = 0;
    return eqw(cls, want);
}

static int fd_fill(HDC dc, const RECT* rc, HBRUSH br) {
    RECT t;
    if (!rc || !br || !p_FillRect) return 0;
    t = *rc;
    p_FillRect(dc, &t, br);
    return 1;
}

int hook_DrawThemeBackground_fd(void* theme, HDC dc, int part, int state,
                                const RECT* rc, const RECT* clip) {
    if (!on_ui_thread())
        return p_real_DrawThemeBackground
            ? p_real_DrawThemeBackground(theme, dc, part, state, rc, clip)
            : (int)0x80004001;
    if (rc) {
        if (fd_class_is(dc, L"SysTabControl32")) {
            HBRUSH br = (state == 1 && g_fd_tab_inactive) ? g_fd_tab_inactive : g_fd_fill;
            if (br && p_FillRect) {
                RECT t = *rc;
                p_FillRect(dc, &t, br);
                /* RE: comctl32 draws the pane/body as part 9 and each top
                   tab item as parts 5..8 (state 3 = selected).
                   - pane: one FrameRect outlines the selected tab's
                     contents and the line under the unselected tabs
                   - item: one FrameRect outlines the tab; the selected
                     tab's bottom is opened so it joins the pane
                   No per-edge bookkeeping. */
                if (part == 9) {
                    g_fd_pane = t;
                    g_fd_pane_valid = 1;
                    if (p_FrameRect && g_fd_edge &&
                        t.right - t.left >= 3 && t.bottom - t.top >= 3)
                        p_FrameRect(dc, &t, g_fd_edge);
                } else if (part >= 5 && part <= 8) {
                    if (p_FrameRect && g_fd_edge &&
                        t.right - t.left >= 3 && t.bottom - t.top >= 3) {
                        RECT b = t;
                        b.top = b.bottom - 1;
                        if (state != 3) {
                            b.left += 1;
                            b.right -= 1;
                        }
                        p_FrameRect(dc, &t, g_fd_edge);
                        p_FillRect(dc, &b, br);
                        if (state != 3 && part != 7 && part != 8) {
                            RECT e = t;
                            e.left = e.right - 1;
                            p_FillRect(dc, &e, br);
                        }
                        if (state == 3) {
                            if (g_fd_pane_valid && g_fd_edge) {
                                RECT v;
                                v.top = g_fd_pane.top;
                                v.bottom = t.bottom;
                                if (v.bottom <= v.top) v.bottom = v.top + 1;
                                if (t.left <= g_fd_pane.left) {
                                    v.left = g_fd_pane.left;
                                    v.right = v.left + 1;
                                    p_FillRect(dc, &v, g_fd_edge);
                                }
                                if (t.right >= g_fd_pane.right) {
                                    v.left = g_fd_pane.right - 1;
                                    v.right = g_fd_pane.right;
                                    p_FillRect(dc, &v, g_fd_edge);
                                }
                            }
                        }
                    }
                }
                return 0;
            }
        }
        if (fd_class_is(dc, L"msctls_statusbar32")) {
            if (fd_fill(dc, rc, g_fd_fill)) return 0;
        }
    }
    return p_real_DrawThemeBackground
        ? p_real_DrawThemeBackground(theme, dc, part, state, rc, clip)
        : (int)0x80004001;
}

void theme_init(HMODULE ux, HMODULE g, HMODULE u) {
    (void)g;
    p_real_GetThemeColor = (PFN_GetThemeColor)resolve(ux, "GetThemeColor");
    p_real_DrawThemeBackground = (PFN_DrawThemeBackground)resolve(ux, "DrawThemeBackground");
    p_fd_wfdc = (PFN_WindowFromDC)resolve(u, "WindowFromDC");
    if (p_CreateSolidBrush) {
        g_fd_fill = p_CreateSolidBrush(CLR_BG);
        g_fd_tab_inactive = p_CreateSolidBrush(DC_LIGHT);
        g_fd_edge = p_CreateSolidBrush(DC_VIS_EDGE);
    }
}

void theme_attach(void) {
    if (p_real_GetThemeColor)
        hook_attach((void**)&p_real_GetThemeColor, (void*)&hook_GetThemeColor);
    if (p_real_DrawThemeBackground)
        hook_attach((void**)&p_real_DrawThemeBackground, (void*)&hook_DrawThemeBackground_fd);
}

void theme_start_thread(void) {
    /* no background thread needed — export hook is immediate */
}
