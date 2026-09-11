#pragma once
#include <windows.h>
#include "str_obf.h" // OBF() — compile-time string obfuscation for all log format strings

// Minimal logger: a log file next to the DLL (mw120rproxy.log), OutputDebugString,
// and the console when one is open. Thread-safe (single mutex). No CRT global
// state beyond a FILE* opened lazily.
namespace log120r {
void Init(HMODULE self, bool openConsole);
void Linef(const char* fmt, ...);
// Queue already formatted engine text for the external console. Never calls
// an engine print API or performs console/file I/O on the calling thread.
void EngineConsole(unsigned channel, int flags, const char* text);
void Shutdown();
}

// fmt must be a string literal (OBF requires it). Trailing args follow via MSVC comma-elision.
#define LOG(fmt, ...) ::log120r::Linef(OBF(fmt), ##__VA_ARGS__)
