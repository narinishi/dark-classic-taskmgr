#include "common.h"

int hook_begin(void) {
    int s = MH_Initialize();
    return (s == MH_OK || s == MH_ERROR_ALREADY_INITIALIZED) ? 0 : 1;
}

long hook_attach(void** ppPointer, void* pDetour) {
    if (!ppPointer || !*ppPointer || !pDetour) return 1;
    return MH_CreateHook(*ppPointer, pDetour, ppPointer) == MH_OK ? 0 : 1;
}

long hook_commit(void) {
    return MH_EnableHook(MH_ALL_HOOKS) == MH_OK ? 0 : 1;
}

void* resolve(HMODULE mod, const char* name) {
    if (!mod) return 0;
    return iat_GetProcAddress(mod, name);
}

int on_ui_thread(void) {
    if (!p_GetCurrentThreadId) return 1;
    return p_GetCurrentThreadId() == g_ui_tid;
}

/* ---- debug log (file lives in the project tree) ---- */
#if !UHOOKS_NO_DBG
static void* dbg_h = 0;
static unsigned int sstrlen(const char* s) { unsigned int n = 0; while (s[n]) ++n; return n; }

void dbg(const char* s) {
    DWORD wrote = 0;
    if (!p_CreateFileW || !p_WriteFile) return;
    if (!dbg_h) {
        dbg_h = p_CreateFileW(L"C:\\Users\\Administrator\\Documents\\pmn\\tooling\\classictm\\_reversing\\uhooks4\\uhooks4_debug.log",
                              0x40000000u, 3, 0, 4, 0, 0);
    }
    if (dbg_h == (void*)-1) return;
    p_WriteFile(dbg_h, s, sstrlen(s), &wrote, 0);
}

void dbg_hex(unsigned long long v) {
    char buf[19];
    int i;
    for (i = 15; i >= 0; --i) {
        int nib = (int)((v >> (i * 4)) & 0xF);
        buf[15 - i] = (char)(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
    buf[16] = ' '; buf[17] = 0;
    dbg(buf);
}
#endif /* !UHOOKS_NO_DBG */

int eqw(const wchar_t* a, const wchar_t* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

int eqi(const char* a, const char* b) {
    unsigned char ca, cb;
    for (;;) {
        ca = (unsigned char)*a++; cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
}

int streq(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

void patch_slot(void* slot, void* fn) {
    DWORD old;
    if (!slot || !fn) return;
    if (p_VirtualProtect) {
        p_VirtualProtect(slot, sizeof(void*), 0x04u, &old);
        *(void**)slot = fn;
        p_VirtualProtect(slot, sizeof(void*), old, &old);
    } else {
        *(void**)slot = fn;
    }
}

int abs_int(int v) { return v < 0 ? -v : v; }

int lum_of(COLORREF c) {
    int r = (int)(c & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)((c >> 16) & 0xFF);
    return (r * 299 + g * 587 + b * 114) / 1000;
}
