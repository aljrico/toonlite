#ifndef TOON_R_H
#define TOON_R_H

// R interop shared by the readers, the writers and the .Call entry points.
// Include the C++ standard headers you need before this one: R's headers
// define macros (length, error, ...) that collide with the standard library,
// so they are removed right after inclusion.

#include <csetjmp>
#include <cstddef>
#include <string>
#include <string_view>

#include <R.h>
#include <Rinternals.h>
#ifdef length
#undef length
#endif
#ifdef error
#undef error
#endif
#ifdef Realloc
#undef Realloc
#endif
#ifdef Free
#undef Free
#endif

namespace toonlite {

// Thrown when R code called from C++ (a user callback) signals a condition or
// jumps. The .Call wrapper resumes the jump with R_ContinueUnwind() after the
// C++ stack has been unwound, so the user's error reaches R unchanged.
struct UnwindException {
    SEXP token;
};

// Poll for a user interrupt. The interrupt unwinds the C++ stack as an
// UnwindException before R sees it.
void check_interrupt();

// Call `fn(arg)` so that an R error inside it unwinds the C++ stack first.
// `arg` must be protected by the caller.
SEXP call_r_function(SEXP fn, SEXP arg);

inline SEXP mkchar_utf8(std::string_view s) {
    return Rf_mkCharLenCE(s.data(), static_cast<int>(s.size()), CE_UTF8);
}

}  // namespace toonlite

#endif  // TOON_R_H
