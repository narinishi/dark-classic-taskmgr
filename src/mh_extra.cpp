// mh_extra.cpp - additional Win32 passthroughs required by MinHook.
// Same pattern as shim.cpp: resolve once from kernel32 and forward.

#include <windows.h>
#include <tlhelp32.h>

extern "C" {
extern HMODULE (WINAPI *iat_LoadLibraryW)(LPCWSTR);
extern FARPROC (WINAPI *iat_GetProcAddress)(HMODULE, LPCSTR);
}

static HMODULE k32() {
    static HMODULE h = 0;
    if (!h) h = iat_LoadLibraryW(L"kernel32.dll");
    return h;
}

#define RESOLVE(name) ((PFN_##name)iat_GetProcAddress(k32(), #name))

typedef DWORD(WINAPI* PFN_GetCurrentProcessId)(void);
extern "C" DWORD WINAPI GetCurrentProcessId(void) {
    PFN_GetCurrentProcessId p = RESOLVE(GetCurrentProcessId);
    return p ? p() : 0;
}

typedef BOOL(WINAPI* PFN_CloseHandle)(HANDLE);
extern "C" BOOL WINAPI CloseHandle(HANDLE a) {
    PFN_CloseHandle p = RESOLVE(CloseHandle);
    return p ? p(a) : FALSE;
}

typedef HANDLE(WINAPI* PFN_CreateToolhelp32Snapshot)(DWORD, DWORD);
extern "C" HANDLE WINAPI CreateToolhelp32Snapshot(DWORD a, DWORD b) {
    PFN_CreateToolhelp32Snapshot p = RESOLVE(CreateToolhelp32Snapshot);
    return p ? p(a, b) : INVALID_HANDLE_VALUE;
}

typedef BOOL(WINAPI* PFN_Thread32First)(HANDLE, LPTHREADENTRY32);
extern "C" BOOL WINAPI Thread32First(HANDLE a, LPTHREADENTRY32 b) {
    PFN_Thread32First p = RESOLVE(Thread32First);
    return p ? p(a, b) : FALSE;
}

typedef BOOL(WINAPI* PFN_Thread32Next)(HANDLE, LPTHREADENTRY32);
extern "C" BOOL WINAPI Thread32Next(HANDLE a, LPTHREADENTRY32 b) {
    PFN_Thread32Next p = RESOLVE(Thread32Next);
    return p ? p(a, b) : FALSE;
}

typedef HANDLE(WINAPI* PFN_OpenThread)(DWORD, BOOL, DWORD);
extern "C" HANDLE WINAPI OpenThread(DWORD a, BOOL b, DWORD c) {
    PFN_OpenThread p = RESOLVE(OpenThread);
    return p ? p(a, b, c) : 0;
}

typedef void(WINAPI* PFN_Sleep)(DWORD);
extern "C" void WINAPI Sleep(DWORD a) {
    PFN_Sleep p = RESOLVE(Sleep);
    if (p) p(a);
}

typedef void(WINAPI* PFN_GetSystemInfo)(LPSYSTEM_INFO);
extern "C" void WINAPI GetSystemInfo(LPSYSTEM_INFO a) {
    PFN_GetSystemInfo p = RESOLVE(GetSystemInfo);
    if (p) p(a);
}

typedef HANDLE(WINAPI* PFN_HeapCreate)(DWORD, SIZE_T, SIZE_T);
extern "C" HANDLE WINAPI HeapCreate(DWORD a, SIZE_T b, SIZE_T c) {
    PFN_HeapCreate p = RESOLVE(HeapCreate);
    return p ? p(a, b, c) : 0;
}

typedef BOOL(WINAPI* PFN_HeapDestroy)(HANDLE);
extern "C" BOOL WINAPI HeapDestroy(HANDLE a) {
    PFN_HeapDestroy p = RESOLVE(HeapDestroy);
    return p ? p(a) : FALSE;
}

typedef LPVOID(WINAPI* PFN_HeapAlloc)(HANDLE, DWORD, SIZE_T);
extern "C" LPVOID WINAPI HeapAlloc(HANDLE a, DWORD b, SIZE_T c) {
    PFN_HeapAlloc p = RESOLVE(HeapAlloc);
    return p ? p(a, b, c) : 0;
}

typedef LPVOID(WINAPI* PFN_HeapReAlloc)(HANDLE, DWORD, LPVOID, SIZE_T);
extern "C" LPVOID WINAPI HeapReAlloc(HANDLE a, DWORD b, LPVOID c, SIZE_T d) {
    PFN_HeapReAlloc p = RESOLVE(HeapReAlloc);
    return p ? p(a, b, c, d) : 0;
}

typedef BOOL(WINAPI* PFN_HeapFree)(HANDLE, DWORD, LPVOID);
extern "C" BOOL WINAPI HeapFree(HANDLE a, DWORD b, LPVOID c) {
    PFN_HeapFree p = RESOLVE(HeapFree);
    return p ? p(a, b, c) : FALSE;
}

extern "C" LONG WINAPI InterlockedExchange(LONG volatile* t, LONG v) {
    return __atomic_exchange_n(t, v, __ATOMIC_SEQ_CST);
}

extern "C" LONG WINAPI InterlockedCompareExchange(LONG volatile* t, LONG v, LONG c) {
    LONG e = c;
    __atomic_compare_exchange_n(t, &e, v, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return e;
}
