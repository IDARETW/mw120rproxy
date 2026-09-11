#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

// Fault-free reads/writes of game memory. We validate every address with
// VirtualQuery (which never faults) and only then touch it. This keeps the detours
// safe even if the game (or an anti-tamper layer) has a vectored exception handler
// that would turn a "guarded" bad read into a fatal dialog before our SEH frame runs.
namespace safemem {
inline bool RegionReadable(const void* p, size_t& avail) {
    avail = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & PAGE_GUARD)
        return false;
    const DWORD prot = mbi.Protect & 0xFF;
    const bool ok = prot == PAGE_READONLY || prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
                    prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
                    prot == PAGE_EXECUTE_WRITECOPY;
    if (!ok)
        return false;
    avail = (size_t)(((const char*)mbi.BaseAddress + mbi.RegionSize) - (const char*)p);
    return true;
}

inline bool RegionWritable(const void* p) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & PAGE_GUARD)
        return false;
    const DWORD prot = mbi.Protect & 0xFF;
    return prot == PAGE_READWRITE || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READWRITE ||
           prot == PAGE_EXECUTE_WRITECOPY;
}

// Copy a C string out of game memory into dst (always NUL-terminated). Returns
// the number of chars copied. Never faults; truncates at the region edge.
inline size_t ReadString(const char* src, char* dst, size_t dstSize) {
    if (dstSize == 0)
        return 0;
    dst[0] = 0;
    if (!src)
        return 0;
    size_t avail = 0;
    if (!RegionReadable(src, avail))
        return 0;
    size_t maxN = dstSize - 1;
    if (maxN > avail)
        maxN = avail;
    size_t i = 0;
    for (; i < maxN && src[i]; ++i)
        dst[i] = src[i]; // inside a committed page -> safe
    dst[i] = 0;
    return i;
}

inline bool ReadBytes(const void* src, void* dst, size_t len) {
    size_t avail = 0;
    if (!RegionReadable(src, avail) || avail < len)
        return false;
    std::memcpy(dst, src, len);
    return true;
}

inline bool WriteByte(void* dst, uint8_t v) {
    if (!dst || !RegionWritable(dst))
        return false;
    *(volatile uint8_t*)dst = v;
    return true;
}

inline bool WriteBytes(void* dst, const void* src, size_t len) {
    if (!dst || len == 0)
        return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(dst, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD))
        return false;
    const DWORD prot = mbi.Protect & 0xFF;
    const bool writable = prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
                          prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
    if (!writable)
        return false;
    const size_t room = (size_t)(((char*)mbi.BaseAddress + mbi.RegionSize) - (char*)dst);
    if (room < len)
        return false;
    std::memcpy(dst, src, len);
    return true;
}

// Is the address committed + executable (sanity-check a function pointer before calling)?
inline bool RegionExecutable(const void* p) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD))
        return false;
    const DWORD prot = mbi.Protect & 0xFF;
    return prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
           prot == PAGE_EXECUTE_WRITECOPY;
}
}
