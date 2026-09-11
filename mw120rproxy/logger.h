#pragma once
// Compatibility shim so the files ported verbatim from mw164proxy (inline_hook.cpp, trigger.cpp,
// dvar_patches.cpp) build against mw120rproxy's lean logger (log.cpp). Routes the categorised
// LOG_*/CON macros to log120r::Linef, and provides logger::Trace.
#include "log.h"

// cat + fmt are both string literals, so adjacent-literal concatenation forms one final
// format literal, which OBF() encrypts at compile time (absent from the binary; decrypted
// at runtime). Trailing args follow via MSVC comma-elision (, ##__VA_ARGS__). fmt is
// REQUIRED and must be a literal — every existing call site already passes one.
#define LOG_INFO(cat, fmt, ...) ::log120r::Linef(OBF("[INFO][" cat "] " fmt), ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...) ::log120r::Linef(OBF("[WARN][" cat "] " fmt), ##__VA_ARGS__)
#define LOG_ERR(cat, fmt, ...) ::log120r::Linef(OBF("[ERR ][" cat "] " fmt), ##__VA_ARGS__)
// CON's category is an enum tag in mw124; here we just stringify it.
#define CON(cat, fmt, ...) ::log120r::Linef(OBF("[" #cat "] " fmt), ##__VA_ARGS__)

namespace logger {
// Verbose per-dvar trace -> mw120rproxy.trace.log (the Dvar_RegisterVariant firehose).
void Trace(const char* tag, const char* fmt, ...);
}
