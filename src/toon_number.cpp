#include "toon_number.h"
#include "toon_charconv.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace toonlite {

size_t format_int64(int64_t v, char* buf) {
    auto res = std::to_chars(buf, buf + kNumberBufSize, v);
    return static_cast<size_t>(res.ptr - buf);
}

size_t format_double(double v, char* buf) {
    if (v == 0.0) {  // also -0
        buf[0] = '0';
        return 1;
    }
    // Integers below 2^53 are exact, and their digits are already the
    // shortest round-trip form.
    if (std::fabs(v) < 9007199254740992.0 && v == std::trunc(v)) {
        return format_int64(static_cast<int64_t>(v), buf);
    }

    // Shortest round-trip digits: a k-digit decimal with k <= 15 that maps
    // back to v is unique and equals v rounded to 15 digits, and for 16 or 17
    // digits the correctly rounded candidate is the closest one that
    // round-trips, so trying 15, 16 and 17 significant digits in order gives
    // the fewest digits and, among those, the one closest to v.
    char tmp[kNumberBufSize];
    int len = 0;
    for (int prec = 15; prec <= 17; prec++) {
        len = std::snprintf(tmp, sizeof(tmp), "%.*e", prec - 1, v);
        if (prec == 17) break;
        double back = 0.0;
        auto res = double_from_chars(tmp, tmp + len, back);
        if (res.ec == std::errc{} && back == v) break;
    }

    // tmp is [-]d.ddddde(+|-)XX: collect the digits and the exponent.
    const char* p = tmp;
    const bool neg = (*p == '-');
    if (neg) p++;
    char digits[24];
    int nd = 0;
    while (*p && *p != 'e') {
        if (*p != '.') digits[nd++] = *p;
        p++;
    }
    int exp10 = std::atoi(p + 1);
    while (nd > 1 && digits[nd - 1] == '0') nd--;

    char* out = buf;
    if (neg) *out++ = '-';
    if (exp10 >= -6 && exp10 < 21) {
        if (exp10 >= 0) {
            // digits[0..exp10] before the point, zero padded
            for (int i = 0; i <= exp10; i++) *out++ = (i < nd) ? digits[i] : '0';
            if (nd > exp10 + 1) {
                *out++ = '.';
                for (int i = exp10 + 1; i < nd; i++) *out++ = digits[i];
            }
        } else {
            *out++ = '0';
            *out++ = '.';
            for (int i = 0; i < -exp10 - 1; i++) *out++ = '0';
            for (int i = 0; i < nd; i++) *out++ = digits[i];
        }
    } else {
        *out++ = digits[0];
        if (nd > 1) {
            *out++ = '.';
            for (int i = 1; i < nd; i++) *out++ = digits[i];
        }
        *out++ = 'e';
        *out++ = exp10 < 0 ? '-' : '+';
        auto res = std::to_chars(out, buf + kNumberBufSize, exp10 < 0 ? -exp10 : exp10);
        out = res.ptr;
    }
    return static_cast<size_t>(out - buf);
}

}  // namespace toonlite
