#ifndef TOON_NUMBER_H
#define TOON_NUMBER_H

// Number text in both directions: canonical output (spec section 2), shared
// by the encoders and by the column builders when they turn numbers into
// text, and a fast exact path for reading decimal tokens.

#include <cstddef>
#include <cstdint>

namespace toonlite {

// Large enough for any output of format_double() / format_int64().
constexpr size_t kNumberBufSize = 40;

// Shortest round-trip digits; plain decimal when |v| is 0 or in
// [1e-6, 1e21), otherwise exponent form with a lowercase 'e' and an explicit
// sign (1e-7, 1.5e+21). -0 becomes 0. `v` must be finite. Returns the length
// written to `buf` (not NUL-terminated).
size_t format_double(double v, char* buf);

size_t format_int64(int64_t v, char* buf);

// Correctly rounded value of a token that matches the JSON number grammar,
// computed exactly with integer arithmetic when it has at most 19
// significant digits and a decimal exponent within +-19 (nearly every
// number in practice). Returns false otherwise, or where 128-bit integers
// are unavailable; the caller then uses strtod.
bool parse_decimal(const char* p, size_t n, double& out);

}  // namespace toonlite

#endif  // TOON_NUMBER_H
