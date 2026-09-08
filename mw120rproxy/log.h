#pragma once
#include <windows.h>

// Minimal logger: a log file next to the DLL (mw120rproxy.log), OutputDebugString,
// and the console when one is open. Thread-safe (single mutex). No CRT global
// state beyond a FILE* opened lazily.
namespace log120r
{
    void Init(HMODULE self, bool openConsole);
    void Linef(const char* fmt, ...);
    // Queue already formatted engine text for the external console. Never calls
    // an engine print API or performs console/file I/O on the calling thread.
    void EngineConsole(unsigned channel, int flags, const char* text);
    void Shutdown();
}

// Optional format arguments use MSVC comma elision.
#define LOG(fmt, ...) ::log120r::Linef(fmt, ##__VA_ARGS__)
