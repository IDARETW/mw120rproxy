#pragma once
#include <windows.h>

// -----------------------------------------------------------------------------
// Import hiding for mw120rproxy.
//
// The sensitive Win32 APIs the proxy uses (LoadLibraryW, GetProcAddress, CreateThread, VirtualAlloc/
// Free, GetSystemDirectoryW, GetModuleHandle*) are resolved at RUNTIME instead of being linked as
// imports, so they never appear in this DLL's import table (`dumpbin /imports` no longer advertises
// "I load DLLs, spawn threads, and allocate executable memory"). Resolution is done via ntdll's own
// loader: we hand-parse ONLY ntdll's export table (ntdll is the base module — its exports are real
// addresses, never forwarders), grab LdrGetProcedureAddress, and let it resolve everything else —
// which correctly follows the kernel32 -> kernelbase forwarders + apiset redirections a naive export
// parser would get wrong. All module/function names are OBF'd so they aren't in .rdata either.
//
// NOT usable before .text is decrypted/mapped (if this build turns out to need that at all) — this
// runs from decrypted .text, well after load (first XInput call / InitThread), so ntdll + kernel32
// are always present by then.
// -----------------------------------------------------------------------------
namespace imp {
// Base of the main process image (== GetModuleHandleW(nullptr)), read straight from the PEB. No import.
uintptr_t MainImageBase();

// Handle of an already-loaded module by name (e.g. L"kernel32.dll"), via the PEB Ldr list. Case-
// insensitive. Returns nullptr if the module isn't loaded. (Does not LoadLibrary.)
HMODULE GetModule(const wchar_t* name);

// GetProcAddress replacement (LdrGetProcedureAddress under the hood — resolves forwarders/apisets).
void* GetProc(HMODULE mod, const char* name);

// Loaded-module enumeration (reuses the PEB-Ldr walk). baseName/fullPath are the raw UNICODE_STRING
// buffers — NOT NUL-terminated — with their lengths in wchar_t. cb returns false to stop early; ctx is
// passed through.
struct ModuleInfo {
    void* base;
    const wchar_t* baseName;
    unsigned baseLen;
    const wchar_t* fullPath;
    unsigned fullLen;
};
void EnumModules(bool (*cb)(const ModuleInfo&, void*), void* ctx);

// Typed accessors for the APIs we route off the IAT (resolve-once, cached). Each returns a benign
// failure value if resolution somehow fails (never crashes).
HMODULE LoadLib(const wchar_t* path);                   // kernel32!LoadLibraryW
UINT GetSystemDir(wchar_t* buf, UINT size);             // kernel32!GetSystemDirectoryW
HANDLE CreateThr(LPTHREAD_START_ROUTINE fn, void* arg); // kernel32!CreateThread(0,0,fn,arg,0,0)
void* VAlloc(void* addr, size_t size, DWORD type, DWORD prot); // kernel32!VirtualAlloc
BOOL VFree(void* addr, size_t size, DWORD type);               // kernel32!VirtualFree
}
