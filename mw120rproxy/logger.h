#pragma once

#include "log.h"

// Category and format are adjacent string literals.
#define LOG_INFO(cat, fmt, ...) ::log120r::Linef("[INFO][" cat "] " fmt, ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...) ::log120r::Linef("[WARN][" cat "] " fmt, ##__VA_ARGS__)
#define LOG_ERR(cat, fmt, ...) ::log120r::Linef("[ERR ][" cat "] " fmt, ##__VA_ARGS__)
#define CON(cat, fmt, ...) ::log120r::Linef("[" #cat "] " fmt, ##__VA_ARGS__)

namespace logger {
// Verbose per-dvar trace -> mw120rproxy.trace.log (the Dvar_RegisterVariant firehose).
void Trace(const char* tag, const char* fmt, ...);
}
