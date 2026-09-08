#include "custom_map_loader.h"
#include "custom_maps.h"
#include "replay_bindings.h"
#include "logger.h"
#include "safemem.h"
#include <atomic>
#include <string>
#include <cstring>

namespace {
using OpenFile = HANDLE(__fastcall*)(const char*, unsigned);
std::atomic<OpenFile> g_original{nullptr};
using ResetReader = uintptr_t (*)(uintptr_t);
using InitInflator = uintptr_t (*)(uintptr_t, unsigned, uintptr_t, uintptr_t);
std::atomic<ResetReader> g_reset{nullptr};
InitInflator g_initInflator = nullptr;
std::atomic<bool> g_ready{false};
uintptr_t __fastcall ResetSelectedReader(uintptr_t reader) {
    const DWORD saved = GetLastError();
    uintptr_t state = 0, descriptor = 0;
    unsigned char header[0x88]{};
    unsigned char signedFile = 1;
    char name[64]{};
    bool selectedStored = false;
    // The signed auth subheader includes the original fastfile name. A
    // declared shader dependency must authenticate as that exact source,
    // while the owning custom zone retains its normal unload lifecycle.
    if (safemem::ReadBytes(reinterpret_cast<void*>(reader), &state, sizeof(state)) && state &&
        safemem::ReadBytes(reinterpret_cast<void*>(reader + 0xB0), &descriptor,
                           sizeof(descriptor)) &&
        descriptor &&
        safemem::ReadBytes(reinterpret_cast<void*>(descriptor + 0x50), &signedFile, 1) &&
        signedFile == 1 &&
        safemem::ReadBytes(reinterpret_cast<void*>(descriptor + 0x88), header, sizeof(header)) &&
        memcmp(header, "IWffa100\x0B\0\0\0\xF7\x0F\0\0\x01\0\0\0", 20) == 0) {
        const auto count =
            safemem::ReadString(reinterpret_cast<const char*>(descriptor), name, sizeof(name));
        try {
            std::string zone(name), source;
            if (count && count < sizeof(name) - 1 && zone.ends_with(".ff")) {
                zone.resize(zone.size() - 3);
                if (custommaps::ActiveShaderSource(zone.c_str(), source)) {
                    uint32_t compressionInfo = 0;
                    safemem::ReadBytes(reinterpret_cast<void*>(descriptor + 0x54), &compressionInfo,
                                       sizeof(compressionInfo));
                    LOG_INFO("Maps", "authenticating shader dependency '%s' as original '%s'", name,
                             source.c_str());
                    SetLastError(saved);
                    // InitInflator D881C0 copies the name into its auth state.
                    // Kind 1 retains the native signature/hash verification.
                    return g_initInflator(state, 1, reinterpret_cast<uintptr_t>(source.c_str()),
                                          compressionInfo ? descriptor + 0x54 : 0);
                }
            }
        } catch (...) {
        }
    }
    if (safemem::ReadBytes(reinterpret_cast<void*>(reader), &state, sizeof(state)) && state &&
        safemem::ReadBytes(reinterpret_cast<void*>(reader + 0xB0), &descriptor,
                           sizeof(descriptor)) &&
        descriptor &&
        safemem::ReadBytes(reinterpret_cast<void*>(descriptor + 0x50), &signedFile, 1) &&
        !signedFile &&
        safemem::ReadBytes(reinterpret_cast<void*>(descriptor + 0x88), header, sizeof(header)) &&
        memcmp(header, "IWffc100", 8) == 0 && header[8] == 11 && header[9] == 0 &&
        header[10] == 0 && header[11] == 0 && header[12] == 0xF7 && header[13] == 0x0F &&
        header[14] == 0 && header[15] == 0 && header[16] == 0 && header[17] == 0) {
        const auto count =
            safemem::ReadString(reinterpret_cast<const char*>(descriptor), name, sizeof(name));
        try {
            std::string zone(name);
            if (count && count < sizeof(name) - 1 && zone.ends_with(".ff")) {
                zone.resize(zone.size() - 3);
                std::wstring path;
                selectedStored = custommaps::ActiveZonePath(zone.c_str(), path);
            }
        } catch (...) {
            selectedStored = false;
        }
    }
    if (!selectedStored) {
        SetLastError(saved);
        return g_reset.load(std::memory_order_acquire)(reader);
    }
    // Replay's header parser already accepts IWffc100 and sets descriptor+0x50
    // to zero. Only the reset wrapper rejects it. Reproduce that wrapper's
    // remaining call with kind=0; keep the stock signed path completely intact.
    uint32_t compressionInfo = 0;
    safemem::ReadBytes(reinterpret_cast<void*>(descriptor + 0x54), &compressionInfo,
                       sizeof(compressionInfo));
    LOG_INFO("Maps", "initializing native unsigned stored decoder for selected '%s'", name);
    SetLastError(saved);
    return g_initInflator(state, 0, descriptor, compressionInfo ? descriptor + 0x54 : 0);
}
HANDLE __fastcall OpenSelected(const char* filename, unsigned flags) {
    const DWORD entryError = GetLastError();
    char request[1024]{};
    const auto length = safemem::ReadString(filename, request, sizeof(request));
    std::string replacement;
    bool redirect = false;
    try {
        redirect = length && length < sizeof(request) - 1 &&
                   custommaps::ResolveDiskRead(request, replacement);
    } catch (...) {
        LOG_ERR("Maps", "selected package path resolution failed; native request retained");
    }
    SetLastError(entryError);
    const HANDLE result = g_original.load(std::memory_order_acquire)(
        redirect ? replacement.c_str() : filename, flags);
    const DWORD exitError = GetLastError();
    if (redirect)
        LOG_INFO("Maps", "native disk open '%s' -> '%s': %s flags=0x%X", request,
                 replacement.c_str(), result != INVALID_HANDLE_VALUE ? "opened" : "FAILED", flags);
    SetLastError(exitError);
    return result;
}
}

namespace customloader {
hook::Status Install(uintptr_t base) {
    const bool already = Ready();
    if (already)
        return hook::Status::Installed;
    uint8_t bytes[64]{};
    if (!safemem::ReadBytes(reinterpret_cast<void*>(base + replay::InitInflator.rva), bytes,
                            replay::InitInflator.size) ||
        memcmp(bytes, replay::InitInflator.bytes, replay::InitInflator.size) != 0)
        return hook::Status::NotReady;
    g_initInflator = reinterpret_cast<InitInflator>(base + replay::InitInflator.rva);
    auto status = hook::Install(reinterpret_cast<void*>(base + replay::DiskOpen.rva), &OpenSelected,
                                replay::DiskOpen.bytes, replay::DiskOpen.size, g_original);
    if (status != hook::Status::Installed)
        return status;
    status =
        hook::Install(reinterpret_cast<void*>(base + replay::ResetReader.rva), &ResetSelectedReader,
                      replay::ResetReader.bytes, replay::ResetReader.size, g_reset);
    if (!already && status == hook::Status::Installed) {
        g_ready.store(true, std::memory_order_release);
        LOG_INFO(
            "Maps",
            "selected package disk routing installed at Sys_OpenFileReliable RVA 0x13F4D50; native handle/read/close ownership retained");
    }
    return status;
}
bool Ready() {
    return g_ready.load(std::memory_order_acquire);
}
}
