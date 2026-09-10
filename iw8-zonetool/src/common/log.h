// log.h — printf-style logging for iw8-zonetool. Header-only.
// info()/warn()/err() go to stderr with a tag prefix; fatal() prints + exits(1).
// Verbosity gate: set g_logLevel (0=err only, 1=+warn, 2=+info[default], 3=+debug).
#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace zt
{

inline int g_logLevel = 2; // 0 err, 1 +warn, 2 +info, 3 +debug

namespace detail
{
inline void vlog(FILE *out, const char *tag, const char *fmt, va_list ap)
{
    std::fputs(tag, out);
    std::vfprintf(out, fmt, ap);
    std::fputc('\n', out);
    std::fflush(out);
}
} // namespace detail

inline void info(const char *fmt, ...)
{
    if (g_logLevel < 2)
        return;
    va_list ap;
    va_start(ap, fmt);
    detail::vlog(stderr, "[info] ", fmt, ap);
    va_end(ap);
}
inline void warn(const char *fmt, ...)
{
    if (g_logLevel < 1)
        return;
    va_list ap;
    va_start(ap, fmt);
    detail::vlog(stderr, "[warn] ", fmt, ap);
    va_end(ap);
}
inline void err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    detail::vlog(stderr, "[err ] ", fmt, ap);
    va_end(ap);
}
inline void debug(const char *fmt, ...)
{
    if (g_logLevel < 3)
        return;
    va_list ap;
    va_start(ap, fmt);
    detail::vlog(stderr, "[dbg ] ", fmt, ap);
    va_end(ap);
}

// fatal: print to stderr and terminate the process with code 1.
[[noreturn]] inline void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    detail::vlog(stderr, "[FATAL] ", fmt, ap);
    va_end(ap);
    std::exit(1);
}

} // namespace zt
