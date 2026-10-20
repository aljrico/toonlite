#ifndef TOON_NUMBER_H
#define TOON_NUMBER_H

// Canonical number text (spec section 2), shared by the encoders and by the
// column builders when they turn numbers into text.

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

}  // namespace toonlite

#endif  // TOON_NUMBER_H
