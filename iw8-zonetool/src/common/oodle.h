// oodle.h — runtime loader for oo2core_7_win64.dll (the ONLY external runtime DLL this tool may touch,
// SPEC §5/§8.1). LoadLibraryA the DLL (default: the 1.24 game path; override via --oodle <path> or
// next-to-exe), GetProcAddress OodleLZ_Compress / OodleLZ_Decompress. Degrades gracefully (clear error,
// available()==false) when the DLL is absent — the rest of the tool still runs (stored/zlib codecs).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace zt {

// OodleLZ_Compressor values (subset; Kraken is the IW8 default).
enum class OodleCompressor : int {
    Kraken = 8,
    Mermaid = 9,
    Selkie = 11,
    Leviathan = 13,
};
// OodleLZ_CompressionLevel (subset).
enum class OodleLevel : int {
    None = 0,
    SuperFast = 1,
    VeryFast = 2,
    Fast = 3,
    Normal = 4,
    Optimal1 = 5,
    Optimal2 = 6,
};

class Oodle {
public:
    // Try to load the DLL. Search order: explicit `path` arg (if non-empty), then next-to-exe, then
    // the default 1.24 game install path. Returns true if loaded + both procs resolved.
    static bool load(const std::string& path = std::string());

    // True once load() resolved the DLL + entry points.
    static bool available();

    // Last error string (empty if none / loaded OK).
    static const std::string& last_error();

    // The default game-install DLL path probed when no --oodle override is given.
    static const char* default_dll_path();

    // Compress `in` with Oodle (Kraken/Optimal2 by default). Returns false (and clears out) on failure
    // or if Oodle is unavailable. `out` holds the compressed bytes.
    static bool compress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out,
                         OodleCompressor c = OodleCompressor::Kraken,
                         OodleLevel level = OodleLevel::Optimal2);

    // Decompress `in` into a buffer of EXACTLY `rawLen` bytes (the caller must know the decompressed
    // size — for a .ff that is XFile.size). Returns false on failure or if unavailable.
    static bool decompress(const std::vector<uint8_t>& in, size_t rawLen, std::vector<uint8_t>& out);
};

} // namespace zt
