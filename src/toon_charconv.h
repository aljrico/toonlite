#ifndef TOON_CHARCONV_H
#define TOON_CHARCONV_H

// Portable double parsing.
//
// std::from_chars for floating point is only used where the standard library
// advertises it through __cpp_lib_to_chars (libstdc++ >= 11, MSVC). libc++
// does not define that macro (and Apple's libc++ marks the double overload
// unavailable), so libc++ builds on macOS *and* Linux, as well as older
// toolchains, fall back to std::strtod. R keeps LC_NUMERIC at "C", so strtod
// always uses '.' as the decimal separator and no locale handling is needed.
//
// Define TOONLITE_FORCE_STRTOD to force the fallback (used to test it on
// toolchains that would otherwise take the std::from_chars branch).

#if defined(__has_include)
#if __has_include(<version>)
#include <version>
#endif
#endif

#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && \
    !defined(TOONLITE_FORCE_STRTOD)
#define TOONLITE_STD_FROM_CHARS_DOUBLE 1
#else
#define TOONLITE_STD_FROM_CHARS_DOUBLE 0
#endif

namespace toonlite {

// Same contract as std::from_chars(first, last, double&) with the default
// chars_format::general: on success returns {end of match, errc{}} and sets
// value; otherwise value is left untouched.
inline std::from_chars_result double_from_chars(const char* first,
                                                const char* last,
                                                double& value) {
#if TOONLITE_STD_FROM_CHARS_DOUBLE
    return std::from_chars(first, last, value);
#else
    if (first == last) {
        return {first, std::errc::invalid_argument};
    }

    // std::from_chars rejects a leading '+' and leading whitespace, and does
    // not understand hex floats (for "0x1p3" it matches only the "0");
    // strtod accepts all three. Filter them for parity.
    if (*first == '+' || std::isspace(static_cast<unsigned char>(*first))) {
        return {first, std::errc::invalid_argument};
    }
    {
        const bool neg = (*first == '-');
        const char* p = neg ? first + 1 : first;
        if (last - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            value = neg ? -0.0 : 0.0;
            return {p + 1, std::errc{}};
        }
    }

    // strtod needs a null-terminated string: stack buffer for typical
    // numbers, heap for outliers.
    const std::size_t len = static_cast<std::size_t>(last - first);
    constexpr std::size_t kBuf = 128;
    char stack_buf[kBuf];
    std::string heap_buf;
    const char* buf;
    if (len < kBuf) {
        std::memcpy(stack_buf, first, len);
        stack_buf[len] = '\0';
        buf = stack_buf;
    } else {
        heap_buf.assign(first, len);
        buf = heap_buf.c_str();
    }

    char* end = nullptr;
    errno = 0;
    const double tmp = std::strtod(buf, &end);
    const char* stop = first + (end - buf);

    if (end == buf) {
        return {first, std::errc::invalid_argument};
    }
    // ERANGE: overflow (+-HUGE_VAL) or underflow. Some libcs also flag
    // subnormal results, which std::from_chars accepts, so only treat
    // infinities and underflow to zero as out of range.
    if (errno == ERANGE && (std::isinf(tmp) || tmp == 0.0)) {
        return {stop, std::errc::result_out_of_range};
    }
    value = tmp;
    return {stop, std::errc{}};
#endif
}

}  // namespace toonlite

#endif  // TOON_CHARCONV_H
