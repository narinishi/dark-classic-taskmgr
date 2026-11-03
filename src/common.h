#ifndef UHOOKS4_COMMON_H
#define UHOOKS4_COMMON_H

#include <stddef.h>

/* Debug logging is compiled out by default (saves strings/code).
   Build with -DUHOOKS_NO_DBG=0 to re-enable the file log. */
#ifndef UHOOKS_NO_DBG
#define UHOOKS_NO_DBG 1
#endif

typedef unsigned long DWORD;
typedef unsigned long long UINT_PTR;
typedef unsigned long long WPARAM;
typedef long long LPARAM;
typedef long long LRESULT;
typedef long long LONG_PTR;
typedef unsigned long long SIZE_T64;
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned int COLORREF;
typedef const wchar_t* LPCWSTR;
typedef void* HWND;
typedef void* HDC;
typedef void* HMODULE;
typedef void* HBRUSH;
typedef void* HANDLE;
typedef struct { long left, top, right, bottom; } RECT;

#define TRUE 1
#define FALSE 0
#define RGB_(r,g,b) ((COLORREF)((r) | ((g) << 8) | ((b) << 16)))

typedef HMODULE (*PFN_GetModuleHandleW)(LPCWSTR);
typedef HMODULE (*PFN_LoadLibraryW)(LPCWSTR);
typedef void* (*PFN_GetProcAddress)(HMODULE, const char*);
typedef DWORD (*PFN_GetCurrentProcessId)(void);
typedef BOOL (*PFN_VirtualProtect)(void*, UINT_PTR, DWORD, DWORD*);
typedef DWORD (*PFN_VirtualQuery)(const void*, void*, DWORD);
typedef HBRUSH (*PFN_CreateSolidBrush)(COLORREF);
typedef DWORD (*PFN_GetSysColor)(int);
typedef HBRUSH (*PFN_GetSysColorBrush)(int);
typedef DWORD (*PFN_GetCurrentThreadId)(void);
typedef DWORD (*PFN_GetWindowThreadProcessId)(HWND, DWORD*);
typedef int (*PFN_GetClassNameW)(HWND, wchar_t*, int);
typedef LONG_PTR (*PFN_SetWindowLongPtrW)(HWND, int, LONG_PTR);
typedef LRESULT (*PFN_CallWindowProcW)(LONG_PTR, HWND, UINT, WPARAM, LPARAM);
typedef BOOL (*PFN_InvalidateRect)(HWND, const void*, BOOL);
typedef int (*PFN_DrawTextW)(HDC, const wchar_t*, int, void*, UINT);
typedef HANDLE (*PFN_SetWinEventHook)(DWORD, DWORD, HMODULE, void*, DWORD, DWORD, DWORD);
typedef int (*PFN_SetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
typedef BOOL (*PFN_EnumChildWindows)(HWND, BOOL (*)(HWND, LPARAM), LPARAM);
typedef LONG_PTR (*PFN_GetWindowLongPtrW)(HWND, int);
typedef BOOL (*PFN_RedrawWindow)(HWND, const RECT*, void*, UINT);
typedef LRESULT (*PFN_SendMessageW)(HWND, UINT, WPARAM, LPARAM);
typedef void* (*PFN_CreateFileW)(LPCWSTR, DWORD, DWORD, void*, DWORD, DWORD, void*);
typedef BOOL (*PFN_WriteFile)(void*, const void*, DWORD, DWORD*, void*);
typedef int (*PFN_FillRect)(HDC, const RECT*, HBRUSH);
typedef int (*PFN_FrameRect)(HDC, const RECT*, HBRUSH);
typedef int (*PFN_GetObjectW)(void*, int, void*);
typedef BOOL (*PFN_DeleteObject)(void*);
typedef void* (*PFN_GetCurrentObject)(HDC, UINT);
typedef void* (*PFN_SelectObject)(HDC, void*);
typedef void* (*PFN_GetStockObject)(int);
typedef void* (*PFN_CreatePen)(int, int, COLORREF);
typedef COLORREF (*PFN_SetTextColor2)(HDC, COLORREF);
typedef HWND (*PFN_WindowFromDC)(HDC);

typedef int (*PFN_DrawThemeBackground)(void*, HDC, int, int, const RECT*, const RECT*);
typedef int (*PFN_DrawThemeText)(void*, HDC, int, int, const wchar_t*, int, DWORD, DWORD, const RECT*);
typedef int (*PFN_DrawThemeTextEx)(void*, HDC, int, int, const wchar_t*, int, DWORD, const RECT*, const void*);
typedef int (*PFN_GetThemeColor)(void*, int, int, int, void*);

extern PFN_LoadLibraryW iat_LoadLibraryW;
extern PFN_GetProcAddress iat_GetProcAddress;
extern PFN_GetModuleHandleW iat_GetModuleHandleW;
extern PFN_GetCurrentProcessId iat_GetCurrentProcessId;
extern PFN_GetClassNameW iat_GetClassNameW;
extern PFN_SetWindowLongPtrW iat_SetWindowLongPtrW;
extern PFN_CallWindowProcW iat_CallWindowProcW;
extern PFN_InvalidateRect iat_InvalidateRect;
extern PFN_GetSysColor iat_GetSysColor;
extern PFN_GetSysColorBrush iat_GetSysColorBrush;
extern PFN_DrawTextW iat_DrawTextW;

int MH_Initialize(void);
int MH_CreateHook(void* pTarget, void* pDetour, void** ppOriginal);
int MH_EnableHook(void* pTarget);
#define MH_ALL_HOOKS ((void*)0)
#define MH_OK 0
#define MH_ERROR_ALREADY_INITIALIZED 1

int hook_begin(void);
long hook_attach(void** ppPointer, void* pDetour);
long hook_commit(void);

extern const wchar_t S_KERNEL32[];
extern const wchar_t S_GDI32[];
extern const wchar_t S_UXTHEME[];
extern const wchar_t S_USER32[];
extern const wchar_t S_COMCTL32[];

/* ---- Pachenko palette (matches uhooks2) ---- */
#ifdef PACHENKO
#define DC_BG    RGB_(26,26,26)
#define DC_FG    RGB_(200,200,200)
#define DC_EDGE  RGB_(2,2,2)
#define DC_LIGHT RGB_(57,51,50)
#define DC_GRAY  RGB_(69,69,69)
#define DC_ACC   RGB_(90,103,121)
#define CLR_TEXT RGB_(200,200,200)
#define DC_MENUHILITE RGB_(57,51,50)
#else
#define DC_BG    RGB_(30,30,30)
#define DC_FG    RGB_(230,230,230)
#define DC_EDGE  RGB_(16,16,16)
#define DC_LIGHT RGB_(58,58,58)
#define DC_GRAY  RGB_(128,128,128)
#define DC_ACC   RGB_(0,120,212)
#define CLR_TEXT RGB_(230,230,230)
#define DC_MENUHILITE RGB_(42,42,42)
#endif
#define CLR_BG DC_BG
/* visible-but-subtle border tone for button/graph outlines on dark bg
   (DC_EDGE 2,2,2 was invisible; this reads as a clear thin edge) */
#define DC_VIS_EDGE RGB_(115,115,115)

/* ---- shared globals (defined in stub.c) ---- */
extern PFN_VirtualProtect p_VirtualProtect;
extern PFN_CreateSolidBrush p_CreateSolidBrush;
extern PFN_GetSysColor p_OrigGetSysColor;
extern PFN_GetSysColorBrush p_OrigGetSysColorBrush;
extern PFN_GetCurrentThreadId p_GetCurrentThreadId;
extern DWORD g_ui_tid;
int on_ui_thread(void);
void dui_init(void);
void dui_attach(void);
void dui_on_window(HWND h);
int dui_should_bypass(HDC dc);
int theme_is_directui(void* th);
extern PFN_FillRect p_FillRect;
extern PFN_FrameRect p_FrameRect;
extern PFN_GetObjectW p_GetObjectW;
extern PFN_SetWinEventHook p_SetWinEventHook;
extern PFN_SetWindowTheme p_SetWindowTheme;
extern PFN_EnumChildWindows p_EnumChildWindows;
extern PFN_GetWindowLongPtrW p_GetWindowLongPtrW;
extern PFN_RedrawWindow p_RedrawWindow;
extern PFN_SendMessageW p_SendMessageW;
extern PFN_CreateFileW p_CreateFileW;
extern PFN_WriteFile p_WriteFile;
extern HBRUSH g_bg_brush;
extern HBRUSH g_edge_brush;
extern HBRUSH g_sysbrush[31];
extern COLORREF g_sysbrushclr[31];

/* ---- shared helpers (util.c) ---- */
#if UHOOKS_NO_DBG
#define dbg(x) ((void)0)
#define dbg_hex(x) ((void)0)
#else
void dbg(const char* s);
void dbg_hex(unsigned long long v);
#endif
void* resolve(HMODULE mod, const char* name);
int eqw(const wchar_t* a, const wchar_t* b);
int eqi(const char* a, const char* b);
int streq(const char* a, const char* b);
void patch_slot(void* slot, void* fn);
int lum_of(COLORREF c);
int abs_int(int v);

/* syscolor.c */
int syscolor(int idx, COLORREF* out);
DWORD hook_syscolor(int idx);
HBRUSH hook_sysbrush(int idx);

/* theme.c (LAYER_D) */
int hook_DrawThemeBackground(void* theme, HDC dc, int part, int state, const RECT* rc, const RECT* clip);
void theme_init(HMODULE ux, HMODULE g, HMODULE u);
void theme_start_thread(void);
void theme_attach(void);
int hook_DrawThemeText(void* theme, HDC dc, int part, int state, const wchar_t* s, int n, DWORD flags, DWORD flags2, const RECT* rc);
int hook_DrawThemeTextEx(void* theme, HDC dc, int part, int state, const wchar_t* s, int n, DWORD flags, const RECT* rc, const void* opt);
int hook_GetThemeColor(void* theme, int part, int state, int prop, void* out);

/* textsink.c (LAYER_T) */
void textsink_init(HMODULE g, HMODULE u);
void textsink_attach(void);
COLORREF textsink_raw_set_text_color(HDC dc, COLORREF c);
int textsink_raw_draw_text(HDC dc, const wchar_t* s, int n, void* rc, UINT fmt);

/* stock.c (ELEMS_STOCK) */
void* hook_GetStockObject(int idx);
void stock_init(HMODULE g);
void stock_attach(void);

/* swtheme.c (LAYER_S) */
void apply_swtheme(HWND h);
void on_event(HANDLE hook, DWORD event, HWND h, long idObject, long idChild, DWORD thread, DWORD time);
void swtheme_init(HMODULE ux);

/* uahmenu.c — menu bar painter (WM_UAHDRAWMENU 0x0091 / 0x0092 subclass) */
void uahmenu_init(HMODULE u, HMODULE g);
void uahmenu_on_top_window(HWND h);

/* listgrid.c — list body column separators (SysListView32 subclass) */

/* graphedge.c — dark NC border for owner-drawn graph/value buttons */
void graphedge_init(HMODULE u, HMODULE g);
void graphedge_subclass_tree(HWND root);

/* titlebar.c — DWM dark caption for top-level windows */
void titlebar_init(HMODULE u);
void titlebar_apply_window(HWND h);

/* combodark.c - dark ComboBox dropdown button */

#endif
