#include "toon_encoder.h"
#include "toon_errors.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace toonlite {

Encoder::Encoder(const EncodeOptions& opts) : opts_(opts) {}

void Encoder::write_indent(int depth) {
    if (opts_.pretty && depth > 0) {
        for (int i = 0; i < depth * opts_.indent; i++) {
            buf_.append_char(' ');
        }
    }
}

void Encoder::write_newline() {
    if (opts_.pretty) {
        buf_.append_char('\n');
    }
}

void Encoder::write_string(const char* s) {
    buf_.append(s, strlen(s));
}

void Encoder::write_escaped_string(const char* s) {
    buf_.append_escaped_string(std::string_view(s));
}

void Encoder::check_special_double(double val) {
    if (opts_.strict) {
        if (std::isnan(val)) {
            throw ParseError("NaN values not allowed in strict mode");
        }
        if (std::isinf(val)) {
            throw ParseError("Inf/-Inf values not allowed in strict mode");
        }
    }
}

void Encoder::encode_null() {
    write_string("null");
}

void Encoder::encode_logical(SEXP x) {
    R_xlen_t n = Rf_xlength(x);
    int* data = LOGICAL(x);

    if (n == 1) {
        if (data[0] == NA_LOGICAL) {
            encode_null();
        } else {
            write_string(data[0] ? "true" : "false");
        }
        return;
    }

    // Array of logicals
    buf_.append_char('[');
    buf_.append(std::to_string(n));
    buf_.append("]:", 2);
    write_newline();

    for (R_xlen_t i = 0; i < n; i++) {
        write_indent(1);
        buf_.append("- ", 2);
        if (data[i] == NA_LOGICAL) {
            encode_null();
        } else {
            write_string(data[i] ? "true" : "false");
        }
        write_newline();
    }
}

void Encoder::encode_integer(SEXP x) {
    R_xlen_t n = Rf_xlength(x);
    int* data = INTEGER(x);

    if (n == 1) {
        if (data[0] == NA_INTEGER) {
            encode_null();
        } else {
            buf_.append(std::to_string(data[0]));
        }
        return;
    }

    // Array of integers
    buf_.append_char('[');
    buf_.append(std::to_string(n));
    buf_.append("]:", 2);
    write_newline();

    for (R_xlen_t i = 0; i < n; i++) {
        write_indent(1);
        buf_.append("- ", 2);
        if (data[i] == NA_INTEGER) {
            encode_null();
        } else {
            buf_.append(std::to_string(data[i]));
        }
        write_newline();
    }
}

void Encoder::encode_double(SEXP x) {
    R_xlen_t n = Rf_xlength(x);
    double* data = REAL(x);

    auto format_double = [this](double val) {
        if (ISNA(val) || ISNAN(val)) {
            check_special_double(val);
            return std::string("null");
        }
        if (std::isinf(val)) {
            check_special_double(val);
            return std::string("null"); // Fallback for non-strict
        }

        std::ostringstream oss;
        oss << std::setprecision(17) << val;
        std::string s = oss.str();

        // Ensure we have decimal point for floats
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) {
            s += ".0";
        }
        return s;
    };

    if (n == 1) {
        buf_.append(format_double(data[0]));
        return;
    }

    // Array of doubles
    buf_.append_char('[');
    buf_.append(std::to_string(n));
    buf_.append("]:", 2);
    write_newline();

    for (R_xlen_t i = 0; i < n; i++) {
        write_indent(1);
        buf_.append("- ", 2);
        buf_.append(format_double(data[i]));
        write_newline();
    }
}

void Encoder::encode_string(SEXP x) {
    R_xlen_t n = Rf_xlength(x);

    if (n == 1) {
        SEXP elem = STRING_ELT(x, 0);
        if (elem == NA_STRING) {
            encode_null();
        } else {
            write_escaped_string(CHAR(elem));
        }
        return;
    }

    // Array of strings
    buf_.append_char('[');
    buf_.append(std::to_string(n));
    buf_.append("]:", 2);
    write_newline();

    for (R_xlen_t i = 0; i < n; i++) {
        write_indent(1);
        buf_.append("- ", 2);
        SEXP elem = STRING_ELT(x, i);
        if (elem == NA_STRING) {
            encode_null();
        } else {
            write_escaped_string(CHAR(elem));
        }
        write_newline();
    }
}

void Encoder::encode_vector(SEXP x, int depth) {
    R_xlen_t n = Rf_xlength(x);

    buf_.append_char('[');
    buf_.append(std::to_string(n));
    buf_.append("]:", 2);
    write_newline();

    for (R_xlen_t i = 0; i < n; i++) {
        write_indent(depth + 1);
        buf_.append("- ", 2);
        encode_value(VECTOR_ELT(x, i), depth + 1);
        write_newline();
    }
}

void Encoder::encode_list(SEXP x, int depth) {
    SEXP names = Rf_getAttrib(x, R_NamesSymbol);
    R_xlen_t n = Rf_xlength(x);

    if (names == R_NilValue || Rf_xlength(names) != n) {
        // Unnamed list = array
        encode_vector(x, depth);
        return;
    }

    // Named list = object
    std::vector<std::pair<std::string, R_xlen_t>> items;
    for (R_xlen_t i = 0; i < n; i++) {
        SEXP name_elem = STRING_ELT(names, i);
        std::string name = (name_elem != NA_STRING) ? CHAR(name_elem) : "";
        items.push_back({name, i});
    }

    // Sort by key if canonical
    if (opts_.canonical) {
        std::sort(items.begin(), items.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    for (size_t j = 0; j < items.size(); j++) {
        const auto& [name, i] = items[j];

        if (depth > 0) {
            write_indent(depth);
        }

        // Write key
        bool needs_quotes = name.empty() || name.find(':') != std::string::npos ||
                            name.find(' ') != std::string::npos ||
                            name.find('"') != std::string::npos;
        if (needs_quotes) {
            write_escaped_string(name.c_str());
        } else {
            buf_.append(name);
        }
        buf_.append(": ", 2);

        SEXP val = VECTOR_ELT(x, i);

        // Check if value needs to be nested
        bool is_complex = (TYPEOF(val) == VECSXP && Rf_xlength(val) > 0) ||
                          (Rf_xlength(val) > 1 && TYPEOF(val) != STRSXP);

        if (is_complex) {
            write_newline();
            encode_value(val, depth + 1);
        } else {
            encode_value(val, depth + 1);
            write_newline();
        }
    }
}

void Encoder::encode_dataframe_tabular(SEXP df, int depth) {
    SEXP names = Rf_getAttrib(df, R_NamesSymbol);
    R_xlen_t ncol = Rf_xlength(df);
    R_xlen_t nrow = (ncol > 0) ? Rf_xlength(VECTOR_ELT(df, 0)) : 0;

    // Write header: [N]{field1,field2,...}:
    buf_.append_char('[');
    buf_.append(std::to_string(nrow));
    buf_.append("]{", 2);

    for (R_xlen_t j = 0; j < ncol; j++) {
        if (j > 0) buf_.append_char(',');
        SEXP name_elem = STRING_ELT(names, j);
        if (name_elem != NA_STRING) {
            buf_.append(std::string_view(CHAR(name_elem)));
        }
    }
    buf_.append("}:", 2);
    write_newline();

    // Write rows
    for (R_xlen_t i = 0; i < nrow; i++) {
        write_indent(depth + 1);

        for (R_xlen_t j = 0; j < ncol; j++) {
            if (j > 0) buf_.append(", ", 2);

            SEXP col = VECTOR_ELT(df, j);

            switch (TYPEOF(col)) {
                case LGLSXP: {
                    int val = LOGICAL(col)[i];
                    if (val == NA_LOGICAL) {
                        write_string("null");
                    } else {
                        write_string(val ? "true" : "false");
                    }
                    break;
                }
                case INTSXP: {
                    // Check if it's a factor
                    SEXP levels = Rf_getAttrib(col, R_LevelsSymbol);
                    if (levels != R_NilValue) {
                        int idx = INTEGER(col)[i];
                        if (idx == NA_INTEGER) {
                            write_string("null");
                        } else {
                            write_escaped_string(CHAR(STRING_ELT(levels, idx - 1)));
                        }
                    } else {
                        int val = INTEGER(col)[i];
                        if (val == NA_INTEGER) {
                            write_string("null");
                        } else {
                            buf_.append(std::to_string(val));
                        }
                    }
                    break;
                }
                case REALSXP: {
                    double val = REAL(col)[i];
                    if (ISNA(val) || ISNAN(val)) {
                        check_special_double(val);
                        write_string("null");
                    } else if (std::isinf(val)) {
                        check_special_double(val);
                        write_string("null");
                    } else {
                        std::ostringstream oss;
                        oss << std::setprecision(17) << val;
                        buf_.append(oss.str());
                    }
                    break;
                }
                case STRSXP: {
                    SEXP elem = STRING_ELT(col, i);
                    if (elem == NA_STRING) {
                        write_string("null");
                    } else {
                        write_escaped_string(CHAR(elem));
                    }
                    break;
                }
                default:
                    write_string("null");
                    break;
            }
        }
        write_newline();
    }
}

void Encoder::encode_dataframe_rows(SEXP df, int depth) {
    encode_dataframe_tabular(df, depth);
}

void Encoder::encode_value(SEXP x, int depth) {
    if (x == R_NilValue) {
        encode_null();
        return;
    }

    // Check for data.frame
    if (Rf_inherits(x, "data.frame")) {
        encode_dataframe_tabular(x, depth);
        return;
    }

    // Check for Date/POSIXct
    if (Rf_inherits(x, "Date")) {
        // Convert to ISO string
        R_xlen_t n = Rf_xlength(x);
        double* data = REAL(x);

        auto format_date = [](double val) -> std::string {
            if (ISNA(val) || !std::isfinite(val)) return "null";

            // Bounds check: R Date supports roughly year 0 to year 9999
            // That's approximately -719528 to 2932896 days from 1970-01-01
            constexpr int64_t MIN_DAYS = -719528;  // ~0000-01-01
            constexpr int64_t MAX_DAYS = 2932896;  // ~9999-12-31

            int64_t days = static_cast<int64_t>(val);
            if (days < MIN_DAYS || days > MAX_DAYS) {
                return "null";  // Out of representable range
            }

            // Convert days since 1970-01-01 to year/month/day
            // Using a well-known algorithm for civil date from days
            // Based on Howard Hinnant's date algorithms
            days += 719468;  // Shift to days since 0000-03-01

            int64_t era = (days >= 0 ? days : days - 146096) / 146097;
            int64_t doe = days - era * 146097;  // day of era [0, 146096]
            int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  // year of era [0, 399]
            int64_t y = yoe + era * 400;
            int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);  // day of year [0, 365]
            int64_t mp = (5*doy + 2) / 153;  // month [0, 11]
            int64_t d = doy - (153*mp + 2) / 5 + 1;  // day [1, 31]
            int64_t m = mp < 10 ? mp + 3 : mp - 9;  // month [1, 12]
            y = y + (m <= 2 ? 1 : 0);

            char buf[32];
            int written = snprintf(buf, sizeof(buf), "\"%04d-%02d-%02d\"",
                                   static_cast<int>(y), static_cast<int>(m), static_cast<int>(d));
            if (written < 0 || written >= static_cast<int>(sizeof(buf))) {
                return "null";  // snprintf error
            }
            return buf;
        };

        if (n == 1) {
            buf_.append(format_date(data[0]));
        } else {
            buf_.append_char('[');
            buf_.append(std::to_string(n));
            buf_.append("]:", 2);
            write_newline();
            for (R_xlen_t i = 0; i < n; i++) {
                write_indent(depth + 1);
                buf_.append("- ", 2);
                buf_.append(format_date(data[i]));
                write_newline();
            }
        }
        return;
    }

    if (Rf_inherits(x, "POSIXct")) {
        R_xlen_t n = Rf_xlength(x);
        double* data = REAL(x);

        auto format_datetime = [](double val) -> std::string {
            if (ISNA(val) || !std::isfinite(val)) return "null";

            // Bounds check: reasonable datetime range (year 0 to year 9999)
            // Seconds since 1970-01-01: ~-62167219200 to ~253402300799
            constexpr int64_t MIN_SECS = -62167219200LL;   // ~0000-01-01
            constexpr int64_t MAX_SECS = 253402300799LL;   // ~9999-12-31

            int64_t secs = static_cast<int64_t>(val);
            if (secs < MIN_SECS || secs > MAX_SECS) {
                return "null";  // Out of representable range
            }

            time_t t = static_cast<time_t>(secs);
            struct tm* tm_info = gmtime(&t);
            if (!tm_info) return "null";
            char buf[64];
            size_t written = strftime(buf, sizeof(buf), "\"%Y-%m-%dT%H:%M:%SZ\"", tm_info);
            if (written == 0) return "null";  // strftime failed
            return buf;
        };

        if (n == 1) {
            buf_.append(format_datetime(data[0]));
        } else {
            buf_.append_char('[');
            buf_.append(std::to_string(n));
            buf_.append("]:", 2);
            write_newline();
            for (R_xlen_t i = 0; i < n; i++) {
                write_indent(depth + 1);
                buf_.append("- ", 2);
                buf_.append(format_datetime(data[i]));
                write_newline();
            }
        }
        return;
    }

    switch (TYPEOF(x)) {
        case NILSXP:
            encode_null();
            break;
        case LGLSXP:
            encode_logical(x);
            break;
        case INTSXP: {
            // Check for factor
            SEXP levels = Rf_getAttrib(x, R_LevelsSymbol);
            if (levels != R_NilValue) {
                // Factor - convert to character
                R_xlen_t n = Rf_xlength(x);
                int* data = INTEGER(x);

                if (n == 1) {
                    if (data[0] == NA_INTEGER) {
                        encode_null();
                    } else {
                        write_escaped_string(CHAR(STRING_ELT(levels, data[0] - 1)));
                    }
                } else {
                    buf_.append_char('[');
                    buf_.append(std::to_string(n));
                    buf_.append("]:", 2);
                    write_newline();
                    for (R_xlen_t i = 0; i < n; i++) {
                        write_indent(depth + 1);
                        buf_.append("- ", 2);
                        if (data[i] == NA_INTEGER) {
                            encode_null();
                        } else {
                            write_escaped_string(CHAR(STRING_ELT(levels, data[i] - 1)));
                        }
                        write_newline();
                    }
                }
            } else {
                encode_integer(x);
            }
            break;
        }
        case REALSXP:
            encode_double(x);
            break;
        case STRSXP:
            encode_string(x);
            break;
        case VECSXP:
            encode_list(x, depth);
            break;
        default:
            // Unsupported type - try to coerce to character
            encode_null();
            break;
    }
}

std::string Encoder::encode(SEXP x) {
    buf_.clear();
    encode_value(x, 0);
    return buf_.str();
}

std::string Encoder::encode_dataframe(SEXP df, bool tabular) {
    buf_.clear();
    if (tabular) {
        encode_dataframe_tabular(df, 0);
    } else {
        encode_dataframe_rows(df, 0);
    }
    return buf_.str();
}

} // namespace toonlite
