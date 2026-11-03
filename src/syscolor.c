#include "common.h"

/* Ground-truthed GetSysColor map (verified against live user32 values):
   4=MENU(240), 15=BTNFACE(240), 18=BTNTEXT(black), 8=WINDOWTEXT(black) */
int syscolor(int idx, COLORREF* out) {
    switch (idx) {
    case 0: case 1: case 2: case 3: case 4: case 5:
    case 10: case 11: case 12: case 15: case 24: case 25:
    case 27: case 28: case 30:
        *out = DC_BG; return 1;
    case 6: case 16: case 21:
        *out = DC_EDGE; return 1;
    case 7: case 8: case 9: case 18: case 23:
        *out = DC_FG; return 1;
    case 13:
        *out = DC_ACC; return 1;
    case 14:
        *out = CLR_TEXT; return 1;
    case 17:
        *out = DC_GRAY; return 1;
    case 19:
        *out = CLR_TEXT; return 1;
    case 20: case 22:
        *out = DC_LIGHT; return 1;
    case 26:
        *out = DC_ACC; return 1;
    case 29:
        *out = DC_MENUHILITE; return 1;
    }
    return 0;
}

DWORD hook_syscolor(int idx) {
    COLORREF c;
    if (!on_ui_thread()) {
        if (p_OrigGetSysColor) return p_OrigGetSysColor(idx);
        return 0;
    }
    if (syscolor(idx, &c)) return (DWORD)c;
    if (p_OrigGetSysColor) return p_OrigGetSysColor(idx);
    return 0;
}

HBRUSH hook_sysbrush(int idx) {
    COLORREF c;
    if (!on_ui_thread())
        return p_OrigGetSysColorBrush ? p_OrigGetSysColorBrush(idx) : 0;
    if (syscolor(idx, &c)) {
        if (idx >= 0 && idx < 31) {
            if (g_sysbrush[idx] && g_sysbrushclr[idx] == c) return g_sysbrush[idx];
            if (!p_CreateSolidBrush) return 0;
            g_sysbrush[idx] = p_CreateSolidBrush(c);
            g_sysbrushclr[idx] = c;
            return g_sysbrush[idx];
        }
        return 0;
    }
    if (p_OrigGetSysColorBrush) return p_OrigGetSysColorBrush(idx);
    return 0;
}
