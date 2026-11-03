// shim.cpp — minimal freestanding bridge between Detours' expectations and
// our stub's dynamic Win32 resolution (via the injected iat_* function
// pointers). Detours itself is Microsoft's real detours.cpp/disasm.cpp,
// compiled separately; this file only supplies the symbols it links against:
//   - operator new / operator delete  (trampoline region bookkeeping)
//   - the ~17 kernel32/user32 exports detours.cpp calls directly
// No CRT, no exception machinery, no system-wide effects — every call is
// resolved at runtime through GetProcAddress on kernel32 inside this
// process only (copy-on-write image pages keep the inline hook local).

#include <windows.h>

// IMPORTANT: these are function-pointer VARIABLES (placed at fixed IAT slot
// addresses by the linker script), matching stub.c's declarations — NOT
// functions. Declaring them as functions makes the compiler emit a direct
// `call <slot>` (executing the slot's pointer bytes as code) instead of
// reading the pointer and calling through it.
extern "C" {
extern HMODULE (WINAPI *iat_LoadLibraryW)(LPCWSTR);
extern FARPROC (WINAPI *iat_GetProcAddress)(HMODULE, LPCSTR);
}

// ---- Minimal CRT memory primitives (no CRT linked) ----
extern "C" void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}
extern "C" void* memset(void* dst, int v, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    for (size_t i = 0; i < n; ++i) d[i] = (unsigned char)v;
    return dst;
}

// ---- DetourGetModuleSize: PE SizeOfImage reader (normally in modules.cpp) ----
extern "C" DWORD WINAPI DetourGetModuleSize(HMODULE hModule) {
    if (!hModule) return 0;
    unsigned char* base = (unsigned char*)hModule;
    if (base[0] != 'M' || base[1] != 'Z') return 0;
    DWORD peoff = *(DWORD*)(base + 0x3C);
    if (*(DWORD*)(base + peoff) != 0x00004550u) return 0;
    return *(DWORD*)(base + peoff + 24 + 56);
}

static HMODULE k32() {
    static HMODULE h = 0;
    if (!h) h = iat_LoadLibraryW(L"kernel32.dll");
    return h;
}

#define RESOLVE(name) ((PFN_##name)iat_GetProcAddress(k32(), #name))

// ---- C++ allocation ----
// Detours allocates trampoline bookkeeping with `new`/`delete`. A simple
// VirtualAlloc-backed allocator with no free is sufficient (these live for
// the process lifetime, matching how we keep brush handles alive).
extern "C" void* malloc_shim(unsigned long long sz) {
    typedef LPVOID(WINAPI *PFN_VirtualAlloc)(LPVOID, SIZE_T, DWORD, DWORD);
    PFN_VirtualAlloc pva = RESOLVE(VirtualAlloc);
    if (!pva) return 0;
    return pva(0, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void* operator new(unsigned long long sz) { return malloc_shim(sz); }
void* operator new[](unsigned long long sz) { return malloc_shim(sz); }
void operator delete(void*) noexcept { /* never freed; process lifetime */ }
void operator delete[](void*) noexcept {}
void operator delete(void*, unsigned long long) noexcept {}
void operator delete[](void*, unsigned long long) noexcept {}

// ---- Win32 API passthroughs detours.cpp links against ----
// Each resolves the real export once and forwards. Signatures match the
// win32 SDK's declarations for x64 (all just standard call convention).

typedef BOOL(WINAPI* PFN_VirtualProtect)(LPVOID, SIZE_T, DWORD, PDWORD);
typedef BOOL(WINAPI* PFN_VirtualProtectEx)(HANDLE, LPVOID, SIZE_T, DWORD, PDWORD);
extern "C" BOOL WINAPI VirtualProtect(LPVOID a, SIZE_T b, DWORD c, PDWORD d) {
    PFN_VirtualProtect p = RESOLVE(VirtualProtect);
    return p ? p(a, b, c, d) : FALSE;
}
extern "C" BOOL WINAPI VirtualProtectEx(HANDLE h, LPVOID a, SIZE_T b, DWORD c, PDWORD d) {
    PFN_VirtualProtectEx p = RESOLVE(VirtualProtectEx);
    return p ? p(h, a, b, c, d) : FALSE;
}

typedef LPVOID(WINAPI* PFN_VirtualAlloc)(LPVOID, SIZE_T, DWORD, DWORD);
extern "C" LPVOID WINAPI VirtualAlloc(LPVOID a, SIZE_T b, DWORD c, DWORD d) {
    PFN_VirtualAlloc p = RESOLVE(VirtualAlloc);
    return p ? p(a, b, c, d) : 0;
}

typedef BOOL(WINAPI* PFN_VirtualFree)(LPVOID, SIZE_T, DWORD);
extern "C" BOOL WINAPI VirtualFree(LPVOID a, SIZE_T b, DWORD c) {
    PFN_VirtualFree p = RESOLVE(VirtualFree);
    return p ? p(a, b, c) : FALSE;
}

typedef SIZE_T(WINAPI* PFN_VirtualQuery)(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T);
extern "C" SIZE_T WINAPI VirtualQuery(LPCVOID a, PMEMORY_BASIC_INFORMATION b, SIZE_T c) {
    PFN_VirtualQuery p = RESOLVE(VirtualQuery);
    return p ? p(a, b, c) : 0;
}
typedef SIZE_T(WINAPI* PFN_VirtualQueryEx)(HANDLE, LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T);
extern "C" SIZE_T WINAPI VirtualQueryEx(HANDLE h, LPCVOID a, PMEMORY_BASIC_INFORMATION b, SIZE_T c) {
    PFN_VirtualQueryEx p = RESOLVE(VirtualQueryEx);
    return p ? p(h, a, b, c) : 0;
}

typedef BOOL(WINAPI* PFN_FlushInstructionCache)(HANDLE, LPCVOID, SIZE_T);
extern "C" BOOL WINAPI FlushInstructionCache(HANDLE a, LPCVOID b, SIZE_T c) {
    PFN_FlushInstructionCache p = RESOLVE(FlushInstructionCache);
    return p ? p(a, b, c) : TRUE;
}

typedef HANDLE(WINAPI* PFN_GetCurrentProcess)(void);
extern "C" HANDLE WINAPI GetCurrentProcess(void) {
    PFN_GetCurrentProcess p = RESOLVE(GetCurrentProcess);
    return p ? p() : 0;
}

typedef HANDLE(WINAPI* PFN_GetCurrentThread)(void);
extern "C" HANDLE WINAPI GetCurrentThread(void) {
    PFN_GetCurrentThread p = RESOLVE(GetCurrentThread);
    return p ? p() : 0;
}

typedef DWORD(WINAPI* PFN_GetCurrentThreadId)(void);
extern "C" DWORD WINAPI GetCurrentThreadId(void) {
    PFN_GetCurrentThreadId p = RESOLVE(GetCurrentThreadId);
    return p ? p() : 0;
}

typedef DWORD(WINAPI* PFN_GetLastError)(void);
extern "C" DWORD WINAPI GetLastError(void) {
    PFN_GetLastError p = RESOLVE(GetLastError);
    return p ? p() : 0;
}
typedef void(WINAPI* PFN_SetLastError)(DWORD);
extern "C" void WINAPI SetLastError(DWORD a) {
    PFN_SetLastError p = RESOLVE(SetLastError);
    if (p) p(a);
}

typedef BOOL(WINAPI* PFN_GetThreadContext)(HANDLE, LPCONTEXT);
extern "C" BOOL WINAPI GetThreadContext(HANDLE a, LPCONTEXT b) {
    PFN_GetThreadContext p = RESOLVE(GetThreadContext);
    return p ? p(a, b) : FALSE;
}
extern "C" BOOL WINAPI SetThreadContext(HANDLE a, const CONTEXT* b) {
    typedef BOOL(WINAPI* PFN)(HANDLE, const CONTEXT*);
    PFN p = (PFN)iat_GetProcAddress(k32(), "SetThreadContext");
    return p ? p(a, b) : FALSE;
}

typedef DWORD(WINAPI* PFN_ResumeThread)(HANDLE);
extern "C" DWORD WINAPI ResumeThread(HANDLE a) {
    PFN_ResumeThread p = RESOLVE(ResumeThread);
    return p ? p(a) : (DWORD)-1;
}
typedef DWORD(WINAPI* PFN_SuspendThread)(HANDLE);
extern "C" DWORD WINAPI SuspendThread(HANDLE a) {
    PFN_SuspendThread p = RESOLVE(SuspendThread);
    return p ? p(a) : (DWORD)-1;
}

typedef HMODULE(WINAPI* PFN_GetModuleHandleW)(LPCWSTR);
extern "C" HMODULE WINAPI GetModuleHandleW(LPCWSTR a) {
    PFN_GetModuleHandleW p = RESOLVE(GetModuleHandleW);
    return p ? p(a) : 0;
}
typedef FARPROC(WINAPI* PFN_GetProcAddress)(HMODULE, LPCSTR);
extern "C" FARPROC WINAPI GetProcAddress(HMODULE a, LPCSTR b) {
    PFN_GetProcAddress p = RESOLVE(GetProcAddress);
    return p ? p(a, b) : 0;
}
