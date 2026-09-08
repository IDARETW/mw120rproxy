#include "fastfile_diagnostics.h"

#include "game.h"
#include "logger.h"
#include "safemem.h"

#include <atomic>
#include <cstring>
#include <intrin.h>

namespace
{
    // Verified against Replay DB_File_OpenDBFile at RVA 0xD8CA30.
    struct DBFileHandle
    {
        uint32_t fileStreamIndex;
        uint32_t padding;
        uint64_t dcacheId;
    };
    static_assert(sizeof(DBFileHandle) == 16);
    using Open = bool(__fastcall*)(DBFileHandle*, const char*);
    std::atomic<Open> g_original{nullptr};
    std::atomic<uintptr_t> g_base{0};
    std::atomic<unsigned> g_successes{0};
    std::atomic<unsigned> g_failures{0};

    bool __fastcall ObserveOpen(DBFileHandle* handle, const char* filename)
    {
        const DWORD entryError = GetLastError();
        char name[256]{};
        if (!safemem::ReadString(filename, name, sizeof(name)))
            strcpy_s(name, "<empty or unreadable>");
        SetLastError(entryError);
        const bool opened = g_original.load(std::memory_order_acquire)(handle, filename);
        const DWORD exitError = GetLastError();

        // Keep logging bounded even when the game probes many optional files.
        const unsigned count = opened ? g_successes.fetch_add(1, std::memory_order_relaxed)
                                      : g_failures.fetch_add(1, std::memory_order_relaxed);
        if ((opened && count < 256) || (!opened && count < 32))
        {
            DBFileHandle result{};
            const bool readable = safemem::ReadBytes(handle, &result, sizeof(result));
            const auto base = g_base.load(std::memory_order_relaxed);
            const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
            const auto callerRva = caller >= base && caller - base < 0x1324B000 ? caller - base : 0;
            logger::Trace("Fastfile", "DB_File_OpenDBFile name='%s' opened=%d handle_readable=%d stream=0x%08X dcache=0x%llX caller_rva=0x%llX",
                name, int(opened), int(readable), result.fileStreamIndex,
                (unsigned long long)result.dcacheId, (unsigned long long)callerRva);
            if (!opened)
            {
                LOG_WARN("Fastfile", "native open failed for '%s'; caller RVA 0x%llX, stream=0x%08X dcache=0x%llX (stock result preserved; optional probes can fail)",
                    name, (unsigned long long)callerRva, result.fileStreamIndex,
                    (unsigned long long)result.dcacheId);
                if (count < 4)
                {
                    void* frames[12]{};
                    const auto frameCount = RtlCaptureStackBackTrace(1, 12, frames, nullptr);
                    for (USHORT i = 0; i < frameCount; ++i)
                    {
                        const auto address = reinterpret_cast<uintptr_t>(frames[i]);
                        if (address >= base && address - base < 0x1324B000)
                            LOG_INFO("Fastfile", "open failure frame[%u] game RVA 0x%llX", i,
                                (unsigned long long)(address - base));
                    }
                }
            }
        }
        SetLastError(exitError);
        return opened;
    }
}

namespace fastfilediag
{
    hook::Status Install(uintptr_t moduleBase)
    {
        const bool already = g_original.load(std::memory_order_acquire) != nullptr;
        g_base.store(moduleBase, std::memory_order_relaxed);
        const auto status = hook::Install(reinterpret_cast<void*>(moduleBase + game::kDbFileOpenRVA),
            &ObserveOpen, game::kDbFileOpenPrologue, sizeof(game::kDbFileOpenPrologue), g_original);
        if (status == hook::Status::Installed && !already)
            LOG_INFO("Fastfile", "native DB_File_OpenDBFile observer installed at RVA 0xD8CA30; paths and results unchanged");
        return status;
    }
}
