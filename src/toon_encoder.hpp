#ifndef TOON_ENCODER_HPP
#define TOON_ENCODER_HPP

#include <string>
#include <vector>
#include <cstdint>
#include "toon_io.hpp"

// Forward declaration for R types
#include <R.h>
#include <Rinternals.h>

namespace toonlite {

// Encoder options
struct EncodeOptions {
    bool pretty = true;
    int indent = 2;
    bool strict = true;
    bool canonical = false;  // For format_toon: stable key ordering
};

// Encoder class
class Encoder {
public:
    Encoder(const EncodeOptions& opts = EncodeOptions());

    // Encode R object to TOON string
    std::string encode(SEXP x);

    // Encode data.frame to tabular TOON
    std::string encode_dataframe(SEXP df, bool tabular = true);

private:
    void encode_value(SEXP x, int depth);
    void encode_null();
    void encode_logical(SEXP x);
    void encode_integer(SEXP x);
    void encode_double(SEXP x);
    void encode_string(SEXP x);
    void encode_vector(SEXP x, int depth);
    void encode_list(SEXP x, int depth);
    void encode_dataframe_tabular(SEXP df, int depth);
    void encode_dataframe_rows(SEXP df, int depth);

    // Helpers
    void write_indent(int depth);
    void write_newline();
    void write_string(const char* s);
    void write_escaped_string(const char* s);

    // Check for special values in strict mode
    void check_special_double(double val);

    EncodeOptions opts_;
    WriteBuffer buf_;
};

} // namespace toonlite

#endif // TOON_ENCODER_HPP
