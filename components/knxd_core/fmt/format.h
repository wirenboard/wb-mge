/*
 * Minimal fmt shim for ESP32.
 * Converts std::string args to const char* for printf compatibility.
 */
#ifndef FMT_FORMAT_H_
#define FMT_FORMAT_H_

#include <cstdio>
#include <string>

namespace fmt {
namespace detail {

/* Convert std::string to const char* for printf; pass everything else through */
inline const char* to_printf_arg(const std::string& s) { return s.c_str(); }
template<typename T>
inline const T& to_printf_arg(const T& v) { return v; }

} /* namespace detail */

template<typename... Args>
inline int fprintf(std::FILE* f, const char* format, const Args&... args) {
    return ::fprintf(f, format, detail::to_printf_arg(args)...);
}

template<typename... Args>
inline int printf(const char* format, const Args&... args) {
    return ::printf(format, detail::to_printf_arg(args)...);
}

inline std::string format(const char* s) { return std::string(s); }

template<typename... Args>
inline std::string format(const char* fmt, const Args&... args) {
    char buf[256];
    snprintf(buf, sizeof(buf), fmt, detail::to_printf_arg(args)...);
    return std::string(buf);
}

} /* namespace fmt */

#endif
