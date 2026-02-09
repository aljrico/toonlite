#ifndef TOON_CHARCONV_H
#define TOON_CHARCONV_H

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>

#ifdef _LIBCPP_VERSION
#include <xlocale.h>
#endif

namespace toonlite {

// Apple clang's libc++ does not implement std::from_chars for floating-point
// types.  Provide a thin wrapper that falls back to strtod_l (C locale) on
// libc++.

inline std::from_chars_result double_from_chars(const char* first,
                                                const char* last,
                                                double& value) {
#ifdef _LIBCPP_VERSION
    const std::size_t len = static_cast<std::size_t>(last - first);
    if (len == 0) {
        return {first, std::errc::invalid_argument};
    }

    // std::from_chars (default format) rejects leading '+' and hex floats;
    // strtod accepts both.  Filter them out for consistent cross-platform
    // behavior.
    if (*first == '+') {
        return {first, std::errc::invalid_argument};
    }
    {
        const char* p = (*first == '-') ? first + 1 : first;
        if (last - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            return {first, std::errc::invalid_argument};
        }
    }

    // Null-terminate: stack buffer for typical numbers, heap for outliers
    constexpr std::size_t kBuf = 256;
    char stack_buf[kBuf];
    std::string heap_buf;
    char* buf;
    if (len < kBuf) {
        std::memcpy(stack_buf, first, len);
        stack_buf[len] = '\0';
        buf = stack_buf;
    } else {
        heap_buf.assign(first, len);
        buf = &heap_buf[0];
    }

    // Use strtod_l with the C locale to avoid LC_NUMERIC dependence
    static locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
    char* end = nullptr;
    errno = 0;
    double tmp = strtod_l(buf, &end, c_locale);

    if (end == buf) {
        return {first, std::errc::invalid_argument};
    }
    if (errno == ERANGE) {
        return {first + (end - buf), std::errc::result_out_of_range};
    }
    value = tmp;
    return {first + (end - buf), std::errc{}};
#else
    return std::from_chars(first, last, value);
#endif
}

}  // namespace toonlite

#endif  // TOON_CHARCONV_H
