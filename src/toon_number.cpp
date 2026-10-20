#include "toon_number.h"
#include "toon_charconv.h"

#include <array>
#include <cfloat>
#include <cstdint>
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
    tmp[n++] = 'e';
    n = static_cast<int>(std::to_chars(tmp + n, tmp + sizeof(tmp), d.exp10).ptr - tmp);
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
// to v (spec 2). Searching upwards by printing v at each precision and
// reading it back. A double with a representation of 15 digits or fewer
// has it as its correctly rounded 15-digit form (DBL_DIG), so normal numbers
// start at 15; subnormals have less precision and start at 1. When the
// nearest candidate misses (asymmetric rounding intervals at powers of two),
// its neighbour on the other side of v may still read back.
Decimal shortest_slow(double v) {
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

// Round the 17-digit form of v to `prec` digits. False when the dropped
// digits are exactly half a unit: the 17-digit form is itself rounded, so
// only the exact value can break that tie. `dist` receives the distance
// between the result and the 17-digit form, in units of its last digit.
bool round_17(const Decimal& d17, int prec, Decimal& out, int& dist) {
    int tail = 0, half = 5;
    for (int i = prec; i < 17; i++) tail = tail * 10 + (d17.digits[i] - '0');
    for (int i = prec + 1; i < 17; i++) half *= 10;
    if (tail == half) return false;
    out = d17;
    out.nd = prec;
    if (tail > half) {
        out = neighbour(out, 1);
        dist = 2 * half - tail;
    } else {
        dist = tail;
    }
    return true;
}

// Does a decimal `dist` units (of the 17th digit, u17) away from the
// 17-digit form of v read back as v? The 17-digit form is within 0.5 u17
// of v, and a decimal reads back as v when it is closer to v than half an
// ulp, `h` (in u17). Returns 1 or 0 when that is certain, -1 when it is too
// close to call.
int reads_back(int dist, double h) {
    if (dist + 0.5 < h * (1 - 1e-9)) return 1;
    if (dist - 0.5 > h * (1 + 1e-9)) return 0;
    return -1;
}

#if defined(__SIZEOF_INT128__) && !defined(TOONLITE_NO_INT128)
#define TOONLITE_EXACT_SHORTEST 1
__extension__ typedef unsigned __int128 u128;

constexpr std::array<u128, 23> pow10_table() {
    std::array<u128, 23> t{};
    u128 p = 1;
    for (size_t i = 0; i < t.size(); i++) {
        t[i] = p;
        p *= 10;
    }
    return t;
}
constexpr std::array<u128, 23> kPow10 = pow10_table();

// Exact shortest form for 2^-17 <= |v| < 2^53 (not an integer, not a power
// of two), using integer arithmetic only. With v = M * 2^-s exactly and
// 10^16 <= v * 10^q < 10^17, N = M * 10^q is v in units of 2^-s of the 17th
// significant digit, so rounding v to p digits and checking whether that
// decimal reads back (closer to v than half an ulp, or exactly half with M
// even) are integer comparisons. All products stay below 2^127.
bool shortest_exact(double v, Decimal& out) {
    int e2 = 0;
    const double m = std::frexp(std::fabs(v), &e2);
    if (m == 0.5) return false;  // power of two: asymmetric interval
    const uint64_t M = static_cast<uint64_t>(std::ldexp(m, 53));
    const int s = 53 - e2;  // v = M * 2^-s
    if (s <= 0 || s > 69) return false;
    int E = static_cast<int>(std::floor(std::log10(std::fabs(v))));
    u128 N = 0, D = 0;
    for (int attempt = 0;; attempt++) {
        const int q = 16 - E;
        if (q < 0 || q > 22 || attempt > 2) return false;
        N = static_cast<u128>(M) * kPow10[static_cast<size_t>(q)];
        D = N >> s;
        if (D < kPow10[16]) {
            E--;
        } else if (D >= kPow10[17]) {
            E++;
        } else {
            break;
        }
    }
    const u128 half_ulp2 = kPow10[static_cast<size_t>(16 - E)];  // 2 * half an ulp, scaled
    for (int p = DBL_DIG; p <= 17; p++) {
        const uint64_t unit = static_cast<uint64_t>(kPow10[static_cast<size_t>(17 - p)]);
        uint64_t C = static_cast<uint64_t>(D) / unit;
        // v's remainder below C, scaled by 2^s, against half a unit
        const u128 rem = N - ((static_cast<u128>(C) * unit) << s);
        const u128 unit_s = static_cast<u128>(unit) << s;
        if (2 * rem > unit_s || (2 * rem == unit_s && (C & 1))) C++;
        const u128 c = (static_cast<u128>(C) * unit) << s;
        const u128 diff = c > N ? c - N : N - c;
        if (p < 17 && (2 * diff > half_ulp2 || (2 * diff == half_ulp2 && (M & 1)))) continue;
        char buf[24];
        const int len = static_cast<int>(std::to_chars(buf, buf + sizeof(buf), C).ptr - buf);
        std::memcpy(out.digits, buf, static_cast<size_t>(len));
        out.nd = len;
        out.exp10 = E + len - p;  // C = 10^p after a carry has p + 1 digits
        return true;
    }
    return false;
}
#endif

// shortest_slow() with less printing and parsing. Most values people write
// have 15 digits or fewer and are settled by the 15-digit attempt. Otherwise
// v is printed once to 17 digits, the 16-digit candidate is rounded from
// that, and whether it reads back is decided from its distance to v; only
// ties and borderline distances fall back to printf and strtod. Powers of
// two (asymmetric intervals), subnormals and extreme exponents take the
// slow path.
Decimal shortest(double v) {
#if TOONLITE_EXACT_SHORTEST
    {
        Decimal d;
        if (shortest_exact(v, d)) return d;
    }
#endif
    int e2 = 0;
    const double m = std::frexp(std::fabs(v), &e2);  // v = m * 2^e2, m in [0.5, 1)
    if (m == 0.5 || std::fabs(v) < 1e-290 || std::fabs(v) > 1e290) return shortest_slow(v);
    const bool neg = v < 0;
    const Decimal d15 = nearest(v, DBL_DIG);
    if (value_of(d15, neg) == v) return d15;

    const Decimal d17 = nearest(v, 17);
    // half an ulp of v, in units of the 17th significant digit
    const double h = std::ldexp(1.0, e2 - 54) / std::pow(10.0, d17.exp10 - 16);
    Decimal d;
    int dist = 0;
    int ok = -1;
    if (round_17(d17, 16, d, dist)) {
        ok = reads_back(dist, h);
    } else {
        d = nearest(v, 16);
    }
    if (ok == -1) ok = value_of(d, neg) == v;
    if (ok) return d;
    // The other 16-digit neighbour of v.
    const bool d_above = d17.exp10 != d.exp10 ? d.exp10 > d17.exp10
                                              : std::memcmp(d.digits, d17.digits, 16) > 0;
    const Decimal other = neighbour(d, d_above ? -1 : 1);
    if (value_of(other, neg) == v) return other;
    return d17;
}
}  // namespace

#if TOONLITE_EXACT_SHORTEST
namespace {

// GCC and Clang, the compilers with __int128, also have __builtin_clzll.
int bit_length(u128 x) {
    const uint64_t hi = static_cast<uint64_t>(x >> 64);
    const uint64_t lo = static_cast<uint64_t>(x);
    if (hi) return 128 - __builtin_clzll(hi);
    return lo ? 64 - __builtin_clzll(lo) : 0;
}

// q * 2^scale rounded to the nearest double, ties to even; `sticky` says
// that the true value is slightly above q * 2^scale.
double round_to_double(u128 q, bool sticky, int scale) {
    const int bl = bit_length(q);
    if (bl <= 53) return std::ldexp(static_cast<double>(static_cast<uint64_t>(q)), scale);
    const int drop = bl - 53;
    uint64_t mant = static_cast<uint64_t>(q >> drop);
    const u128 rem = q & ((static_cast<u128>(1) << drop) - 1);
    const u128 half = static_cast<u128>(1) << (drop - 1);
    if (rem > half || (rem == half && (sticky || (mant & 1)))) mant++;
    return std::ldexp(static_cast<double>(mant), scale + drop);  // 2^53 is exact too
}

}  // namespace
#endif

bool parse_decimal(const char* p, size_t n, double& out) {
#if TOONLITE_EXACT_SHORTEST
    const char* end = p + n;
    const bool neg = p < end && *p == '-';
    if (neg) p++;
    uint64_t w = 0;
    int digits = 0;  // significant digits in w
    int q = 0;       // value = w * 10^q
    auto digit = [&](char c, bool fraction) {
        if (fraction) q--;
        if (w == 0 && c == '0') return true;  // leading zero
        if (digits == 19) return false;
        w = w * 10 + static_cast<uint64_t>(c - '0');
        digits++;
        return true;
    };
    while (p < end && *p >= '0' && *p <= '9') {
        if (!digit(*p++, false)) return false;
    }
    if (p < end && *p == '.') {
        p++;
        while (p < end && *p >= '0' && *p <= '9') {
            if (!digit(*p++, true)) return false;
        }
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        bool eneg = false;
        if (p < end && (*p == '+' || *p == '-')) eneg = *p++ == '-';
        int e = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            if (e > 10000) return false;
            e = e * 10 + (*p++ - '0');
        }
        q += eneg ? -e : e;
    }
    if (p != end) return false;
    if (w == 0) {
        out = neg ? -0.0 : 0.0;
        return true;
    }
    double v;
    if (q >= 0) {
        if (q > 19) return false;
        v = round_to_double(static_cast<u128>(w) * kPow10[static_cast<size_t>(q)], false, 0);
    } else {
        if (q < -19) return false;
        // w / 10^-q: shift w as far left as 128 bits allow, divide, and
        // keep the remainder as a sticky bit. The quotient has at least
        // 62 bits, so rounding it is exact.
        const uint64_t den = static_cast<uint64_t>(kPow10[static_cast<size_t>(-q)]);
        const int k = 127 - bit_length(w);
        const u128 num = static_cast<u128>(w) << k;
        const u128 quo = num / den;
        v = round_to_double(quo, num - quo * den != 0, -k);
    }
    out = neg ? -v : v;
    return true;
#else
    (void)p;
    (void)n;
    (void)out;
    return false;
#endif
}

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
