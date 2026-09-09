// oodle.cpp — LoadLibrary/GetProcAddress wrapper for oo2core_7_win64.dll. No link-time dependency.
#include "oodle.h"
#include "log.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstring>

namespace zt {

// ---- Oodle ABI (oo2core_7) ------------------------------------------------------------------------
// sint64 OodleLZ_Compress(OodleLZ_Compressor, const void* rawBuf, sint64 rawLen, void* compBuf,
//                         OodleLZ_CompressionLevel, void* pOptions, void* dictionaryBase,
//                         void* lrm, void* scratchMem, sint64 scratchSize)
typedef int64_t(__stdcall* OodleLZ_Compress_t)(int compressor, const void* rawBuf, int64_t rawLen,
                                               void* compBuf, int level, const void* pOptions,
                                               const void* dictBase, const void* lrm,
                                               void* scratch, int64_t scratchSize);
// sint64 OodleLZ_Decompress(const void* compBuf, sint64 compLen, void* rawBuf, sint64 rawLen,
//                           fuzz, check, verbose, dictBase, lrm, fpCallback, callbackUserData,
//                           scratch, scratchSize, threadPhase)
typedef int64_t(__stdcall* OodleLZ_Decompress_t)(const void* compBuf, int64_t compLen, void* rawBuf,
                                                int64_t rawLen, int fuzz, int check, int verbose,
                                                void* dictBase, int64_t lrm, void* fpCallback,
                                                void* callbackUserData, void* scratch,
                                                int64_t scratchSize, int threadPhase);
// sint64 OodleLZ_GetCompressedBufferSizeNeeded(compressor, sint64 rawLen)  [oo2core_7 takes 2 args]
typedef int64_t(__stdcall* OodleLZ_GetCompressedBufferSizeNeeded_t)(int compressor, int64_t rawLen);

namespace {
    HMODULE                                   g_dll        = nullptr;
    OodleLZ_Compress_t                        g_compress   = nullptr;
    OodleLZ_Decompress_t                      g_decompress = nullptr;
    OodleLZ_GetCompressedBufferSizeNeeded_t   g_bufsize    = nullptr;
    std::string                               g_error;

    constexpr const char* kDefaultPath =
        "D:\\Games\\iw8\\1.20.4.7623265-replay\\Call of Duty Modern Warfare (1.20.4.7623265)\\oo2core_7_win64.dll";

    std::string exe_dir() {
        char buf[MAX_PATH] = {0};
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return std::string();
        std::string p(buf, n);
        size_t s = p.find_last_of("/\\");
        return (s == std::string::npos) ? std::string() : p.substr(0, s);
    }

    bool try_load(const std::string& path) {
        if (path.empty()) return false;
        HMODULE h = LoadLibraryA(path.c_str());
        if (!h) return false;
        auto c = reinterpret_cast<OodleLZ_Compress_t>(GetProcAddress(h, "OodleLZ_Compress"));
        auto d = reinterpret_cast<OodleLZ_Decompress_t>(GetProcAddress(h, "OodleLZ_Decompress"));
        if (!c || !d) { FreeLibrary(h); return false; }
        g_dll = h; g_compress = c; g_decompress = d;
        g_bufsize = reinterpret_cast<OodleLZ_GetCompressedBufferSizeNeeded_t>(
            GetProcAddress(h, "OodleLZ_GetCompressedBufferSizeNeeded"));
        return true;
    }
}

bool Oodle::load(const std::string& path) {
    if (g_dll) return true; // already loaded
    g_error.clear();

    if (!path.empty() && try_load(path)) { zt::info("oodle: loaded %s", path.c_str()); return true; }

    std::string nextTo = exe_dir();
    if (!nextTo.empty()) {
        std::string p = nextTo + "\\oo2core_7_win64.dll";
        if (try_load(p)) { zt::info("oodle: loaded %s", p.c_str()); return true; }
    }

    if (try_load(kDefaultPath)) { zt::info("oodle: loaded %s", kDefaultPath); return true; }

    g_error = "oo2core_7_win64.dll not found (tried --oodle / next-to-exe / default game path). "
              "Pass --oodle <path>.";
    return false;
}

bool Oodle::available() { return g_dll && g_compress && g_decompress; }
const std::string& Oodle::last_error() { return g_error; }
const char* Oodle::default_dll_path() { return kDefaultPath; }

bool Oodle::compress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out,
                     OodleCompressor c, OodleLevel level) {
    out.clear();
    if (!available()) { g_error = "Oodle not loaded"; return false; }

    int64_t rawLen = static_cast<int64_t>(in.size());
    int64_t bound  = g_bufsize ? g_bufsize(static_cast<int>(c), rawLen)
                               : (rawLen + 274 * ((rawLen + 0x3FFFF) / 0x40000)); // OODLELZ_COMPRESS bound
    if (bound < rawLen + 1024) bound = rawLen + 1024;
    out.resize(static_cast<size_t>(bound));

    int64_t n = g_compress(static_cast<int>(c), in.data(), rawLen, out.data(),
                           static_cast<int>(level), nullptr, nullptr, nullptr, nullptr, 0);
    if (n <= 0) { g_error = "OodleLZ_Compress returned <= 0"; out.clear(); return false; }
    out.resize(static_cast<size_t>(n));
    return true;
}

bool Oodle::decompress(const std::vector<uint8_t>& in, size_t rawLen, std::vector<uint8_t>& out) {
    out.clear();
    if (!available()) { g_error = "Oodle not loaded"; return false; }
    out.resize(rawLen);
    // fuzz=1 (yes), check=0 (none), verbose=0; thread-phase = OodleLZ_Decode_Unthreaded (3).
    int64_t n = g_decompress(in.data(), static_cast<int64_t>(in.size()), out.data(),
                             static_cast<int64_t>(rawLen), 1, 0, 0,
                             nullptr, 0, nullptr, nullptr, nullptr, 0, 3);
    if (n != static_cast<int64_t>(rawLen)) {
        g_error = "OodleLZ_Decompress size mismatch";
        out.clear();
        return false;
    }
    return true;
}

} // namespace zt
