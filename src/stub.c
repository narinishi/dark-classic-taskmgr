#include "common.h"

const wchar_t S_KERNEL32[] = L"kernel32.dll";
const wchar_t S_GDI32[] = L"gdi32.dll";
const wchar_t S_UXTHEME[] = L"uxtheme.dll";
const wchar_t S_USER32[] = L"user32.dll";
const wchar_t S_COMCTL32[] = L"comctl32.dll";

/* ---- shared globals ---- */
PFN_VirtualProtect p_VirtualProtect;
PFN_CreateSolidBrush p_CreateSolidBrush;
PFN_GetSysColor p_OrigGetSysColor;
PFN_GetSysColorBrush p_OrigGetSysColorBrush;
PFN_GetCurrentThreadId p_GetCurrentThreadId;
DWORD g_ui_tid;
PFN_FillRect p_FillRect;
PFN_FrameRect p_FrameRect;
PFN_GetObjectW p_GetObjectW;
PFN_SetWinEventHook p_SetWinEventHook;
PFN_SetWindowTheme p_SetWindowTheme;
PFN_EnumChildWindows p_EnumChildWindows;
PFN_GetWindowLongPtrW p_GetWindowLongPtrW;
PFN_RedrawWindow p_RedrawWindow;
PFN_SendMessageW p_SendMessageW;
PFN_CreateFileW p_CreateFileW;
PFN_WriteFile p_WriteFile;
HBRUSH g_bg_brush;
HBRUSH g_edge_brush;
HBRUSH g_sysbrush[31];
COLORREF g_sysbrushclr[31];

void dark_init(void) {
    HMODULE g, k32, u, ux;
    g = iat_GetModuleHandleW(S_GDI32);
    if (!g) g = iat_LoadLibraryW(S_GDI32);
    k32 = iat_GetModuleHandleW(S_KERNEL32);
    if (!k32) k32 = iat_LoadLibraryW(S_KERNEL32);
    u = iat_GetModuleHandleW(S_USER32);
    if (!u) u = iat_LoadLibraryW(S_USER32);
    ux = iat_LoadLibraryW(S_UXTHEME);

    p_CreateFileW = (PFN_CreateFileW)resolve(k32, "CreateFileW");
    p_WriteFile = (PFN_WriteFile)resolve(k32, "WriteFile");
    dbg("uhooks4 dark_init enter\n");

    p_CreateSolidBrush = (PFN_CreateSolidBrush)resolve(g, "CreateSolidBrush");
    p_VirtualProtect = (PFN_VirtualProtect)resolve(k32, "VirtualProtect");
    p_GetCurrentThreadId = (PFN_GetCurrentThreadId)resolve(k32, "GetCurrentThreadId");
    g_ui_tid = p_GetCurrentThreadId ? p_GetCurrentThreadId() : 0;
    p_FillRect = (PFN_FillRect)resolve(u, "FillRect");
    p_FrameRect = (PFN_FrameRect)resolve(u, "FrameRect");
    p_GetObjectW = (PFN_GetObjectW)resolve(g, "GetObjectW");
    p_SetWinEventHook = (PFN_SetWinEventHook)resolve(u, "SetWinEventHook");
    p_EnumChildWindows = (PFN_EnumChildWindows)resolve(u, "EnumChildWindows");
    p_GetWindowLongPtrW = (PFN_GetWindowLongPtrW)resolve(u, "GetWindowLongPtrW");
    p_RedrawWindow = (PFN_RedrawWindow)resolve(u, "RedrawWindow");
    p_SendMessageW = (PFN_SendMessageW)resolve(u, "SendMessageW");

    p_OrigGetSysColor = (PFN_GetSysColor)resolve(u, "GetSysColor");
    p_OrigGetSysColorBrush = (PFN_GetSysColorBrush)resolve(u, "GetSysColorBrush");

    if (p_CreateSolidBrush) {
        int i;
        g_bg_brush = p_CreateSolidBrush(CLR_BG);
        g_edge_brush = p_CreateSolidBrush(DC_EDGE);
        for (i = 0; i < 31; ++i) {
            COLORREF c;
            if (syscolor(i, &c)) {
                g_sysbrush[i] = p_CreateSolidBrush(c);
                g_sysbrushclr[i] = c;
            }
        }
    }

    theme_init(ux, g, u);
    textsink_init(g, u);
    stock_init(g);
    uahmenu_init(u, g);
    graphedge_init(u, g);
    titlebar_init(u);
    swtheme_init(ux);
    dui_init();

    hook_begin();
    theme_attach();
    textsink_attach();
    stock_attach();
    if (p_OrigGetSysColor)
        hook_attach((void**)&p_OrigGetSysColor, (void*)&hook_syscolor);
    if (p_OrigGetSysColorBrush)
        hook_attach((void**)&p_OrigGetSysColorBrush, (void*)&hook_sysbrush);
    dui_attach();
    hook_commit();

    dbg("uhooks4 dark_init exit\n");
}
