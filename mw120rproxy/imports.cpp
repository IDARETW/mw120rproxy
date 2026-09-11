#include "imports.h"
#include "str_obf.h" // OBF()/OBFW() — keep the module/function names out of .rdata

#include <intrin.h> // __readgsqword
#include <cstdint>
#include <cstring>

// See imports.h. Resolve sensitive APIs at runtime via ntdll's loader so they aren't in the IAT.
namespace imp {
namespace {
// ---- minimal NT string structs (avoid winternl.h to dodge header collisions) ----
struct AnsiStr {
    USHORT Length, MaximumLength;
    PCHAR Buffer;
};
using LdrGetProc_t = LONG(NTAPI*)(void* /*base*/, AnsiStr* /*name*/, ULONG /*ord*/, void** /*out*/);

// ---- PEB access (x64: TEB->PEB at gs:[0x60]) ----
inline uintptr_t Peb() {
    return static_cast<uintptr_t>(__readgsqword(0x60));
}

// Case-insensitive compare: `a` is length-counted (chars), `b` is NUL-terminated. ASCII fold.
bool EqI(const wchar_t* a, size_t alen, const wchar_t* b) {
    size_t i = 0;
    for (; i < alen && b[i]; ++i) {
        wchar_t ca = a[i], cb = b[i];
        if (ca >= L'A' && ca <= L'Z')
            ca = (wchar_t)(ca + 32);
        if (cb >= L'A' && cb <= L'Z')
            cb = (wchar_t)(cb + 32);
        if (ca != cb)
            return false;
    }
    return i == alen && b[i] == 0;
}

// Parse a module's export directory by name. NO forwarder handling — used ONLY for ntdll, whose
// exports are real addresses (ntdll is the bottom of the loader stack). Returns nullptr on miss.
void* RawExport(HMODULE mod, const char* name) {
    if (!mod || !name)
        return nullptr;
    auto base = reinterpret_cast<uint8_t*>(mod);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;
    const IMAGE_DATA_DIRECTORY& d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!d.VirtualAddress || !d.Size)
        return nullptr;
    auto exp = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + d.VirtualAddress);
    auto names = reinterpret_cast<uint32_t*>(base + exp->AddressOfNames);
    auto ords = reinterpret_cast<uint16_t*>(base + exp->AddressOfNameOrdinals);
    auto funcs = reinterpret_cast<uint32_t*>(base + exp->AddressOfFunctions);
    for (uint32_t i = 0; i < exp->NumberOfNames; ++i) {
        const char* en = reinterpret_cast<const char*>(base + names[i]);
        if (std::strcmp(en, name) == 0)
            return base + funcs[ords[i]];
    }
    return nullptr;
}

// The one bootstrap primitive: ntdll!LdrGetProcedureAddress, resolved once by hand-parsing ntdll.
LdrGetProc_t LdrGetProc() {
    static LdrGetProc_t s = reinterpret_cast<LdrGetProc_t>(
        RawExport(GetModule(OBFW(L"ntdll.dll")), OBF("LdrGetProcedureAddress")));
    return s;
}

// Cached kernel32 base (PEB walk).
HMODULE Kernel32() {
    static HMODULE s = GetModule(OBFW(L"kernel32.dll"));
    return s;
}
} // namespace

uintptr_t MainImageBase() {
    return *reinterpret_cast<uintptr_t*>(Peb() + 0x10); // PEB.ImageBaseAddress
}

HMODULE GetModule(const wchar_t* name) {
    if (!name)
        return nullptr;
    const uintptr_t ldr = *reinterpret_cast<uintptr_t*>(Peb() + 0x18); // PEB.Ldr
    if (!ldr)
        return nullptr;
    const uintptr_t head = ldr + 0x10; // InLoadOrderModuleList head
    for (uintptr_t cur = *reinterpret_cast<uintptr_t*>(head); cur && cur != head;
         cur = *reinterpret_cast<uintptr_t*>(cur)) // Flink (InLoadOrderLinks @ +0)
    {
        void* dllBase = *reinterpret_cast<void**>(cur + 0x30);   // LDR_DATA_TABLE_ENTRY.DllBase
        USHORT len = *reinterpret_cast<USHORT*>(cur + 0x58);     // BaseDllName.Length (bytes)
        wchar_t* buf = *reinterpret_cast<wchar_t**>(cur + 0x60); // BaseDllName.Buffer
        if (buf && len && EqI(buf, len / sizeof(wchar_t), name))
            return reinterpret_cast<HMODULE>(dllBase);
    }
    return nullptr;
}

void EnumModules(bool (*cb)(const ModuleInfo&, void*), void* ctx) {
    if (!cb)
        return;
    const uintptr_t ldr = *reinterpret_cast<uintptr_t*>(Peb() + 0x18); // PEB.Ldr
    if (!ldr)
        return;
    const uintptr_t head = ldr + 0x10; // InLoadOrderModuleList head
    for (uintptr_t cur = *reinterpret_cast<uintptr_t*>(head); cur && cur != head;
         cur = *reinterpret_cast<uintptr_t*>(cur)) // Flink (InLoadOrderLinks @ +0)
    {
        ModuleInfo mi{};
        mi.base = *reinterpret_cast<void**>(cur + 0x30); // DllBase
        mi.fullLen = (unsigned)(*reinterpret_cast<USHORT*>(cur + 0x48) /
                                sizeof(wchar_t));               // FullDllName.Length
        mi.fullPath = *reinterpret_cast<wchar_t**>(cur + 0x50); // FullDllName.Buffer
        mi.baseLen = (unsigned)(*reinterpret_cast<USHORT*>(cur + 0x58) /
                                sizeof(wchar_t));               // BaseDllName.Length
        mi.baseName = *reinterpret_cast<wchar_t**>(cur + 0x60); // BaseDllName.Buffer
        if (!cb(mi, ctx))
            return;
    }
}

void* GetProc(HMODULE mod, const char* name) {
    LdrGetProc_t f = LdrGetProc();
    if (!mod || !name || !f)
        return nullptr;
    AnsiStr a;
    const size_t n = std::strlen(name);
    a.Length = static_cast<USHORT>(n);
    a.MaximumLength = static_cast<USHORT>(n + 1);
    a.Buffer = const_cast<PCHAR>(name);
    void* out = nullptr;
    if (f(mod, &a, 0, &out) < 0)
        return nullptr; // NT_ERROR
    return out;
}

HMODULE LoadLib(const wchar_t* path) {
    using Fn = HMODULE(WINAPI*)(LPCWSTR);
    static Fn f = reinterpret_cast<Fn>(GetProc(Kernel32(), OBF("LoadLibraryW")));
    return f ? f(path) : nullptr;
}

UINT GetSystemDir(wchar_t* buf, UINT size) {
    using Fn = UINT(WINAPI*)(LPWSTR, UINT);
    static Fn f = reinterpret_cast<Fn>(GetProc(Kernel32(), OBF("GetSystemDirectoryW")));
    return f ? f(buf, size) : 0;
}

HANDLE CreateThr(LPTHREAD_START_ROUTINE fn, void* arg) {
    using Fn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD,
                               LPDWORD);
    static Fn f = reinterpret_cast<Fn>(GetProc(Kernel32(), OBF("CreateThread")));
    return f ? f(nullptr, 0, fn, arg, 0, nullptr) : nullptr;
}

void* VAlloc(void* addr, size_t size, DWORD type, DWORD prot) {
    using Fn = LPVOID(WINAPI*)(LPVOID, SIZE_T, DWORD, DWORD);
    static Fn f = reinterpret_cast<Fn>(GetProc(Kernel32(), OBF("VirtualAlloc")));
    return f ? f(addr, size, type, prot) : nullptr;
}

BOOL VFree(void* addr, size_t size, DWORD type) {
    using Fn = BOOL(WINAPI*)(LPVOID, SIZE_T, DWORD);
    static Fn f = reinterpret_cast<Fn>(GetProc(Kernel32(), OBF("VirtualFree")));
    return f ? f(addr, size, type) : FALSE;
}
}
