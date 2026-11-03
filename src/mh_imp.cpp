// mh_imp.cpp - satisfy MinGW's dllimport (__imp_*) references for MinHook by
// pointing each import slot at our freestanding wrapper (shim.cpp/mh_extra.cpp).

#include <windows.h>
#include <tlhelp32.h>

extern "C" {

// provided by shim.cpp / mh_extra.cpp
DWORD WINAPI GetCurrentThreadId(void);
DWORD WINAPI GetCurrentProcessId(void);
HANDLE WINAPI GetCurrentProcess(void);
DWORD WINAPI GetLastError(void);
void WINAPI SetLastError(DWORD);
BOOL WINAPI CloseHandle(HANDLE);
HANDLE WINAPI CreateToolhelp32Snapshot(DWORD, DWORD);
BOOL WINAPI Thread32First(HANDLE, LPTHREADENTRY32);
BOOL WINAPI Thread32Next(HANDLE, LPTHREADENTRY32);
HANDLE WINAPI OpenThread(DWORD, BOOL, DWORD);
DWORD WINAPI SuspendThread(HANDLE);
DWORD WINAPI ResumeThread(HANDLE);
void WINAPI Sleep(DWORD);
void WINAPI GetSystemInfo(LPSYSTEM_INFO);
HANDLE WINAPI HeapCreate(DWORD, SIZE_T, SIZE_T);
BOOL WINAPI HeapDestroy(HANDLE);
LPVOID WINAPI HeapAlloc(HANDLE, DWORD, SIZE_T);
LPVOID WINAPI HeapReAlloc(HANDLE, DWORD, LPVOID, SIZE_T);
BOOL WINAPI HeapFree(HANDLE, DWORD, LPVOID);
HMODULE WINAPI GetModuleHandleW(LPCWSTR);
FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR);
LPVOID WINAPI VirtualAlloc(LPVOID, SIZE_T, DWORD, DWORD);
BOOL WINAPI VirtualFree(LPVOID, SIZE_T, DWORD);
BOOL WINAPI VirtualProtect(LPVOID, SIZE_T, DWORD, PDWORD);
SIZE_T WINAPI VirtualQuery(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T);
BOOL WINAPI FlushInstructionCache(HANDLE, LPCVOID, SIZE_T);
BOOL WINAPI GetThreadContext(HANDLE, LPCONTEXT);
BOOL WINAPI SetThreadContext(HANDLE, const CONTEXT*);
LONG WINAPI InterlockedExchange(LONG volatile*, LONG);
LONG WINAPI InterlockedCompareExchange(LONG volatile*, LONG, LONG);

DWORD (WINAPI *__imp_GetCurrentThreadId)(void) = GetCurrentThreadId;
DWORD (WINAPI *__imp_GetCurrentProcessId)(void) = GetCurrentProcessId;
HANDLE (WINAPI *__imp_GetCurrentProcess)(void) = GetCurrentProcess;
DWORD (WINAPI *__imp_GetLastError)(void) = GetLastError;
void (WINAPI *__imp_SetLastError)(DWORD) = SetLastError;
BOOL (WINAPI *__imp_CloseHandle)(HANDLE) = CloseHandle;
HANDLE (WINAPI *__imp_CreateToolhelp32Snapshot)(DWORD, DWORD) = CreateToolhelp32Snapshot;
BOOL (WINAPI *__imp_Thread32First)(HANDLE, LPTHREADENTRY32) = Thread32First;
BOOL (WINAPI *__imp_Thread32Next)(HANDLE, LPTHREADENTRY32) = Thread32Next;
HANDLE (WINAPI *__imp_OpenThread)(DWORD, BOOL, DWORD) = OpenThread;
DWORD (WINAPI *__imp_SuspendThread)(HANDLE) = SuspendThread;
DWORD (WINAPI *__imp_ResumeThread)(HANDLE) = ResumeThread;
void (WINAPI *__imp_Sleep)(DWORD) = Sleep;
void (WINAPI *__imp_GetSystemInfo)(LPSYSTEM_INFO) = GetSystemInfo;
HANDLE (WINAPI *__imp_HeapCreate)(DWORD, SIZE_T, SIZE_T) = HeapCreate;
BOOL (WINAPI *__imp_HeapDestroy)(HANDLE) = HeapDestroy;
LPVOID (WINAPI *__imp_HeapAlloc)(HANDLE, DWORD, SIZE_T) = HeapAlloc;
LPVOID (WINAPI *__imp_HeapReAlloc)(HANDLE, DWORD, LPVOID, SIZE_T) = HeapReAlloc;
BOOL (WINAPI *__imp_HeapFree)(HANDLE, DWORD, LPVOID) = HeapFree;
HMODULE (WINAPI *__imp_GetModuleHandleW)(LPCWSTR) = GetModuleHandleW;
FARPROC (WINAPI *__imp_GetProcAddress)(HMODULE, LPCSTR) = GetProcAddress;
LPVOID (WINAPI *__imp_VirtualAlloc)(LPVOID, SIZE_T, DWORD, DWORD) = VirtualAlloc;
BOOL (WINAPI *__imp_VirtualFree)(LPVOID, SIZE_T, DWORD) = VirtualFree;
BOOL (WINAPI *__imp_VirtualProtect)(LPVOID, SIZE_T, DWORD, PDWORD) = VirtualProtect;
SIZE_T (WINAPI *__imp_VirtualQuery)(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T) = VirtualQuery;
BOOL (WINAPI *__imp_FlushInstructionCache)(HANDLE, LPCVOID, SIZE_T) = FlushInstructionCache;
BOOL (WINAPI *__imp_GetThreadContext)(HANDLE, LPCONTEXT) = GetThreadContext;
BOOL (WINAPI *__imp_SetThreadContext)(HANDLE, const CONTEXT*) = SetThreadContext;
LONG (WINAPI *__imp_InterlockedExchange)(LONG volatile*, LONG) = InterlockedExchange;
LONG (WINAPI *__imp_InterlockedCompareExchange)(LONG volatile*, LONG, LONG) = InterlockedCompareExchange;

} // extern "C"
