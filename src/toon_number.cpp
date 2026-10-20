#include "toon_number.h"
#include "toon_charconv.h"

#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace toonlite {

namespace {

// A decimal d.ddd x 10^exp10 with `nd` significant digits.
struct Decimal {
    char digits[24];
    int nd = 0;
    int exp10 = 0;
};

// The correctly rounded `prec`-digit decimal nearest to |v|.
Decimal nearest(double v, int prec) {
    char tmp[48];
    std::snprintf(tmp, sizeof(tmp), "%.*e", prec - 1, std::fabs(v));
    Decimal d;
    const char* p = tmp;
    while (*p && *p != 'e') {
        if (*p != '.') d.digits[d.nd++] = *p;
        p++;
    }
    d.exp10 = std::atoi(p + 1);
    return d;
}

double value_of(const Decimal& d, bool neg) {
    char tmp[48];
    int n = 0;
    if (neg) tmp[n++] = '-';
    tmp[n++] = d.digits[0];
    tmp[n++] = '.';
    for (int i = 1; i < d.nd; i++) tmp[n++] = d.digits[i];
    if (d.nd == 1) tmp[n++] = '0';
    n += std::snprintf(tmp + n, sizeof(tmp) - static_cast<size_t>(n), "e%d", d.exp10);
    double back = 0.0;
    double_from_chars(tmp, tmp + n, back);
    return back;
}

// The next decimal with the same digit count, one unit in the last place
// up (dir > 0) or down.
Decimal neighbour(Decimal d, int dir) {
    int i = d.nd - 1;
    if (dir > 0) {
        while (i >= 0 && d.digits[i] == '9') d.digits[i--] = '0';
        if (i < 0) {
            d.digits[0] = '1';
            d.exp10++;
        } else {
            d.digits[i]++;
        }
    } else {
        while (i >= 0 && d.digits[i] == '0') d.digits[i--] = '9';
        if (i >= 0) d.digits[i]--;
        if (d.digits[0] == '0') {  // 1000 -> 0999: 9999 one decade down
            for (int k = 0; k < d.nd; k++) d.digits[k] = '9';
            d.exp10--;
        }
    }
    return d;
}

// Fewest significant digits that read back as v; among those, the closest
// to v (spec 2). A double with a representation of 15 digits or fewer has
// it as its correctly rounded 15-digit form (DBL_DIG), so normal numbers
// start at 15; subnormals have less precision and start at 1. When the
// nearest candidate misses (asymmetric rounding intervals at powers of two),
// its neighbour on the other side of v may still read back.
Decimal shortest(double v) {
    const bool subnormal = std::fabs(v) < DBL_MIN;
    const bool neg = v < 0;
    for (int prec = subnormal ? 1 : DBL_DIG; prec < 17; prec++) {
        const Decimal d = nearest(v, prec);
        const double back = value_of(d, neg);
        if (back == v) return d;
        if (prec > DBL_DIG || subnormal) {
            const Decimal other = neighbour(d, (back > v) != neg ? -1 : 1);
            if (value_of(other, neg) == v) return other;
        }
    }
    return nearest(v, 17);
}

}  // namespace

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

    Decimal d = shortest(v);
    while (d.nd > 1 && d.digits[d.nd - 1] == '0') d.nd--;
    const char* digits = d.digits;
    const int nd = d.nd;
    const int exp10 = d.exp10;

    char* out = buf;
    if (v < 0) *out++ = '-';
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
