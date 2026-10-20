#include "toon_encoder.h"
#include "toon_lex.h"
#include "toon_number.h"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace toonlite {

namespace {

// Nesting beyond this is almost certainly a cycle-like structure built by
// mistake; stop before the C stack runs out.
constexpr size_t kMaxDepth = 10000;

bool has_class(SEXP x, const char* cls) { return OBJECT(x) && Rf_inherits(x, cls); }

bool is_df(SEXP x) { return TYPEOF(x) == VECSXP && has_class(x, "data.frame"); }

bool is_atomic(SEXP x) {
    const int t = TYPEOF(x);
    return t == LGLSXP || t == INTSXP || t == REALSXP || t == STRSXP;
}

R_xlen_t df_nrow(SEXP df) {
    if (Rf_xlength(df) > 0) {
        SEXP col = VECTOR_ELT(df, 0);
        return is_df(col) ? df_nrow(col) : Rf_xlength(col);
    }
    return Rf_xlength(Rf_getAttrib(df, R_RowNamesSymbol));
}

// ---------------------------------------------------------------------------
// Strings (spec 7.1-7.3)
// ---------------------------------------------------------------------------

bool numeric_like(std::string_view s) {
    // /^[+-]?[0-9]+(\.[0-9]+)?(e[+-]?[0-9]+)?$/i
    const size_t n = s.size();
    size_t i = 0;
    auto digits = [&]() {
        const size_t start = i;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
        return i > start;
    };
    if (i < n && (s[i] == '+' || s[i] == '-')) i++;
    if (!digits()) return false;
    if (i < n && s[i] == '.') {
        i++;
        if (!digits()) return false;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        if (!digits()) return false;
    }
    return i == n;
}

bool needs_quotes(std::string_view s, char delim, bool root) {
    if (s.empty()) return true;
    const unsigned char first = static_cast<unsigned char>(s.front());
    const unsigned char last = static_cast<unsigned char>(s.back());
    if (first == ' ' || first == '\t' || last == ' ' || last == '\t') return true;
    if (first == '-' || first == '#') return true;
    if (root && s.size() >= 3 && first == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        return true;
    }
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == ':' || c == '"' || c == '\\' || c == '[' || c == ']' || c == '{' ||
            c == '}' || ch == delim) {
            return true;
        }
    }
    if (s == "true" || s == "false" || s == "null") return true;
    if ((first >= '0' && first <= '9') || first == '+') return numeric_like(s);
    return false;
}

void put_escaped(Out& o, std::string_view s) {
    static const char hex[] = "0123456789abcdef";
    o.put('"');
    size_t start = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0x20 && c != '"' && c != '\\') continue;
        o.put(s.substr(start, i - start));
        switch (c) {
            case '"': o.put("\\\""); break;
            case '\\': o.put("\\\\"); break;
            case '\n': o.put("\\n"); break;
            case '\r': o.put("\\r"); break;
            case '\t': o.put("\\t"); break;
            default: {
                const char esc[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 0xF]};
                o.put(std::string_view(esc, 6));
                break;
            }
        }
        start = i + 1;
    }
    o.put(s.substr(start));
    o.put('"');
}

void put_string(Out& o, std::string_view s, char delim, bool root) {
    if (needs_quotes(s, delim, root)) {
        put_escaped(o, s);
    } else {
        o.put(s);
    }
}

bool plain_key(std::string_view k) {
    if (k.empty()) return false;
    const char c0 = k[0];
    if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_')) return false;
    for (size_t i = 1; i < k.size(); i++) {
        const char c = k[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

bool name_eq(SEXP a, SEXP b) { return a == b || char_utf8(a) == char_utf8(b); }

R_xlen_t find_name(SEXP names, R_xlen_t n, SEXP key, R_xlen_t hint) {
    if (hint < n && name_eq(STRING_ELT(names, hint), key)) return hint;
    for (R_xlen_t i = 0; i < n; i++) {
        if (i != hint && name_eq(STRING_ELT(names, i), key)) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Dates (proleptic Gregorian, UTC)
// ---------------------------------------------------------------------------

void civil_from_days(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
}

size_t format_ymd(int64_t days, char* buf) {
    int64_t y;
    unsigned m, d;
    civil_from_days(days, y, m, d);
    const int n = y < 0 ? std::snprintf(buf, 48, "-%04lld-%02u-%02u", static_cast<long long>(-y), m, d)
                        : std::snprintf(buf, 48, "%04lld-%02u-%02u", static_cast<long long>(y), m, d);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

// Days since 1970-01-01; 0 when not representable.
size_t format_date(double days, char* buf) {
    if (!std::isfinite(days) || std::fabs(days) > 1e11) return 0;
    return format_ymd(static_cast<int64_t>(std::floor(days)), buf);
}

// Seconds since 1970-01-01 UTC, truncated to whole seconds.
size_t format_datetime(double secs, char* buf) {
    if (!std::isfinite(secs) || std::fabs(secs) > 1e16) return 0;
    const int64_t s = static_cast<int64_t>(std::floor(secs));
    int64_t days = s / 86400;
    int64_t rem = s % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    size_t n = format_ymd(days, buf);
    if (n == 0) return 0;
    const int k = std::snprintf(buf + n, 48 - n, "T%02d:%02d:%02dZ", static_cast<int>(rem / 3600),
                                static_cast<int>(rem / 60 % 60), static_cast<int>(rem % 60));
    return k > 0 ? n + static_cast<size_t>(k) : 0;
}

double time_value(const PrimVec& v, R_xlen_t i) {
    if (v.ip) return v.ip[i] == NA_INTEGER ? NA_REAL : static_cast<double>(v.ip[i]);
    return v.dp[i];
}

}  // namespace

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

void Out::flush() {
    if (file_ && !buf_.empty()) {
        if (std::fwrite(buf_.data(), 1, buf_.size(), file_) != buf_.size()) {
            throw std::runtime_error("Error writing to file: " + path_);
        }
        buf_.clear();
    }
}

std::string_view char_utf8(SEXP c) {
    if (c == NA_STRING) return "NA";
    if (Rf_getCharCE(c) == CE_BYTES) {
        throw std::runtime_error("Strings marked as \"bytes\" cannot be written as TOON");
    }
    const char* s = Rf_translateCharUTF8(c);
    const size_t n = (s == CHAR(c)) ? static_cast<size_t>(LENGTH(c)) : std::strlen(s);
    if (!valid_utf8(s, n)) {
        throw std::runtime_error("Invalid UTF-8 in a string");
    }
    return std::string_view(s, n);
}

void write_key(Out& o, std::string_view key) {
    if (plain_key(key)) {
        o.put(key);
    } else {
        put_escaped(o, key);
    }
}

PrimVec prim_vec(SEXP x) {
    PrimVec v;
    v.x = x;
    switch (TYPEOF(x)) {
        case LGLSXP:
            v.kind = PrimKind::Logical;
            v.ip = LOGICAL(x);
            break;
        case INTSXP:
            v.ip = INTEGER(x);
            v.kind = PrimKind::Integer;
            if (OBJECT(x)) {
                if (Rf_inherits(x, "factor")) {
                    v.kind = PrimKind::Factor;
                    v.levels = Rf_getAttrib(x, R_LevelsSymbol);
                } else if (Rf_inherits(x, "Date")) {
                    v.kind = PrimKind::Date;
                } else if (Rf_inherits(x, "POSIXct")) {
                    v.kind = PrimKind::DateTime;
                }
            }
            break;
        case REALSXP:
            v.dp = REAL(x);
            v.kind = PrimKind::Double;
            if (OBJECT(x)) {
                if (Rf_inherits(x, "integer64")) {
                    v.kind = PrimKind::Int64;
                } else if (Rf_inherits(x, "Date")) {
                    v.kind = PrimKind::Date;
                } else if (Rf_inherits(x, "POSIXct")) {
                    v.kind = PrimKind::DateTime;
                }
            }
            break;
        case STRSXP:
            v.kind = PrimKind::String;
            break;
        default:
            break;
    }
    return v;
}

void write_prim(Out& o, const PrimVec& v, R_xlen_t i, char delim, bool root) {
    char buf[48];
    switch (v.kind) {
        case PrimKind::Null:
            break;
        case PrimKind::Logical: {
            const int b = v.ip[i];
            if (b != NA_LOGICAL) {
                o.put(b ? std::string_view("true") : std::string_view("false"));
                return;
            }
            break;
        }
        case PrimKind::Integer:
            if (v.ip[i] != NA_INTEGER) {
                o.put(std::string_view(buf, format_int64(v.ip[i], buf)));
                return;
            }
            break;
        case PrimKind::Double: {
            const double d = v.dp[i];
            if (std::isfinite(d)) {
                o.put(std::string_view(buf, format_double(d, buf)));
                return;
            }
            break;  // NA, NaN and +-Inf are null (spec 3)
        }
        case PrimKind::Int64: {
            int64_t k;
            std::memcpy(&k, &v.dp[i], sizeof k);
            if (k != INT64_MIN) {
                o.put(std::string_view(buf, format_int64(k, buf)));
                return;
            }
            break;
        }
        case PrimKind::String: {
            SEXP c = STRING_ELT(v.x, i);
            if (c != NA_STRING) {
                put_string(o, char_utf8(c), delim, root);
                return;
            }
            break;
        }
        case PrimKind::Factor: {
            const int k = v.ip[i];
            if (k != NA_INTEGER && k >= 1 && k <= Rf_xlength(v.levels)) {
                put_string(o, char_utf8(STRING_ELT(v.levels, k - 1)), delim, root);
                return;
            }
            break;
        }
        case PrimKind::Date: {
            const size_t n = format_date(time_value(v, i), buf);
            if (n > 0) {
                put_string(o, std::string_view(buf, n), delim, root);
                return;
            }
            break;
        }
        case PrimKind::DateTime: {
            const size_t n = format_datetime(time_value(v, i), buf);
            if (n > 0) {
                put_string(o, std::string_view(buf, n), delim, root);
                return;
            }
            break;
        }
    }
    o.put("null");
}

// ---------------------------------------------------------------------------
// Encoder: value model
// ---------------------------------------------------------------------------

Encoder::Kind Encoder::kind(Val v) const {
    if (v.i >= 0) return is_df(v.x) ? Kind::Object : Kind::Prim;
    SEXP x = v.x;
    switch (TYPEOF(x)) {
        case LGLSXP:
        case INTSXP:
        case REALSXP:
        case STRSXP:
            return (XLENGTH(x) == 1 && !has_class(x, "AsIs")) ? Kind::Prim : Kind::Array;
        case VECSXP:
            if (is_df(x)) return Kind::Array;
            return Rf_getAttrib(x, R_NamesSymbol) != R_NilValue ? Kind::Object : Kind::Array;
        default:
            return Kind::Prim;
    }
}

R_xlen_t Encoder::obj_size(Val v) const { return Rf_xlength(v.x); }

SEXP Encoder::obj_names(Val v) const { return Rf_getAttrib(v.x, R_NamesSymbol); }

Encoder::Val Encoder::obj_value(Val v, R_xlen_t j) const {
    if (v.i < 0) return Val{VECTOR_ELT(v.x, j), -1};
    SEXP col = VECTOR_ELT(v.x, j);  // row v.i of a data.frame
    if (is_df(col)) return Val{col, v.i};
    if (v.i >= Rf_xlength(col)) return Val{R_NilValue, -1};
    if (TYPEOF(col) == VECSXP) return Val{VECTOR_ELT(col, v.i), -1};
    return Val{col, v.i};
}

R_xlen_t Encoder::arr_size(Val v) const { return is_df(v.x) ? df_nrow(v.x) : Rf_xlength(v.x); }

Encoder::Val Encoder::arr_elem(Val v, R_xlen_t k) const {
    if (TYPEOF(v.x) == VECSXP && !is_df(v.x)) return Val{VECTOR_ELT(v.x, k), -1};
    return Val{v.x, k};
}

void Encoder::line(size_t depth) {
    if (depth > kMaxDepth) {
        throw std::runtime_error("Nesting is deeper than " + std::to_string(kMaxDepth) + " levels");
    }
    o_.line(depth * static_cast<size_t>(opts_.indent));
}

void Encoder::prim(Val v, bool root) {
    if (v.i >= 0) {
        write_prim(o_, prim_vec(v.x), v.i, opts_.delim, root);
    } else if (is_atomic(v.x)) {
        write_prim(o_, prim_vec(v.x), 0, opts_.delim, root);
    } else {
        o_.put("null");
    }
}

void Encoder::check_unique(SEXP names) {
    if (names == checked_names_) return;
    const R_xlen_t n = Rf_xlength(names);
    auto fail = [](SEXP name) {
        throw std::runtime_error("Duplicate name '" + std::string(char_utf8(name)) +
                                 "' in a named list: TOON objects need unique keys");
    };
    if (n <= 32) {
        for (R_xlen_t a = 1; a < n; a++) {
            for (R_xlen_t b = 0; b < a; b++) {
                if (name_eq(STRING_ELT(names, a), STRING_ELT(names, b))) fail(STRING_ELT(names, a));
            }
        }
    } else {
        std::unordered_set<std::string_view> seen;
        seen.reserve(static_cast<size_t>(n));
        for (R_xlen_t a = 0; a < n; a++) {
            if (!seen.insert(char_utf8(STRING_ELT(names, a))).second) fail(STRING_ELT(names, a));
        }
    }
    checked_names_ = names;
}

// ---------------------------------------------------------------------------
// Encoder: form detection (spec 9.3, 9.5)
// ---------------------------------------------------------------------------

bool Encoder::all_prim(Val arr) const {
    if (is_atomic(arr.x)) return true;
    if (is_df(arr.x)) return false;
    const R_xlen_t n = Rf_xlength(arr.x);
    for (R_xlen_t k = 0; k < n; k++) {
        if (kind(arr_elem(arr, k)) != Kind::Prim) return false;
    }
    return true;
}

// Field group `gi` for objects `objs`: every one a non-empty object with the
// same keys, and every column uniform-primitive or nested-uniform.
bool Encoder::build_group(const std::vector<Val>& objs, Fields& f, size_t gi) {
    const Val first = objs[0];
    if (kind(first) != Kind::Object) return false;
    const R_xlen_t k = obj_size(first);
    if (k == 0) return false;
    SEXP names0 = obj_names(first);
    check_unique(names0);

    std::vector<uint8_t> state(static_cast<size_t>(k), 0);  // 1 primitive, 2 object
    for (const Val& o : objs) {
        if (kind(o) != Kind::Object || obj_size(o) != k) return false;
        SEXP names = obj_names(o);
        for (R_xlen_t j = 0; j < k; j++) {
            const R_xlen_t idx = find_name(names, k, STRING_ELT(names0, j), j);
            if (idx < 0) return false;
            const Val v = obj_value(o, idx);
            const Kind kv = kind(v);
            uint8_t& s = state[static_cast<size_t>(j)];
            if (kv == Kind::Prim) {
                if (s == 2) return false;
                s = 1;
            } else if (kv == Kind::Object && obj_size(v) > 0) {
                if (s == 1) return false;
                s = 2;
            } else {
                return false;
            }
        }
    }

    for (R_xlen_t j = 0; j < k; j++) {
        SEXP name = STRING_ELT(names0, j);
        if (state[static_cast<size_t>(j)] == 1) {
            f.groups[gi].push_back(Field{name, -1});
            continue;
        }
        std::vector<Val> sub;
        sub.reserve(objs.size());
        for (const Val& o : objs) {
            sub.push_back(obj_value(o, find_name(obj_names(o), k, name, j)));
        }
        const int g = static_cast<int>(f.groups.size());
        f.groups.emplace_back();
        if (!build_group(sub, f, static_cast<size_t>(g))) return false;
        f.groups[gi].push_back(Field{name, g});
    }
    return true;
}

bool Encoder::tabular(Val arr, Fields& f) {
    const R_xlen_t n = arr_size(arr);
    std::vector<Val> objs;
    objs.reserve(static_cast<size_t>(n));
    for (R_xlen_t k = 0; k < n; k++) objs.push_back(arr_elem(arr, k));
    f.groups.assign(1, {});
    return build_group(objs, f, 0);
}

bool Encoder::keyed(Val obj, Fields& f) {
    const R_xlen_t n = obj_size(obj);
    if (n < 2) return false;
    std::vector<Val> values;
    values.reserve(static_cast<size_t>(n));
    for (R_xlen_t j = 0; j < n; j++) values.push_back(obj_value(obj, j));
    f.groups.assign(1, {});
    return build_group(values, f, 0);
}

// Fast path for a data.frame whose columns are atomic vectors or, nested,
// data.frames of them: the field list comes from the names and every cell is
// written straight from its column.
bool Encoder::df_leaves(SEXP df, Fields& f, size_t gi, std::vector<PrimVec>& leaves) {
    const R_xlen_t ncol = Rf_xlength(df);
    if (ncol == 0) return false;
    SEXP names = Rf_getAttrib(df, R_NamesSymbol);
    if (names == R_NilValue) return false;
    check_unique(names);
    for (R_xlen_t j = 0; j < ncol; j++) {
        SEXP col = VECTOR_ELT(df, j);
        if (is_df(col)) {
            const int g = static_cast<int>(f.groups.size());
            f.groups.emplace_back();
            f.groups[gi].push_back(Field{STRING_ELT(names, j), g});
            if (!df_leaves(col, f, static_cast<size_t>(g), leaves)) return false;
        } else if (is_atomic(col) && Rf_getAttrib(col, R_DimSymbol) == R_NilValue) {
            f.groups[gi].push_back(Field{STRING_ELT(names, j), -1});
            leaves.push_back(prim_vec(col));
        } else {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Encoder: output
// ---------------------------------------------------------------------------

void Encoder::header(R_xlen_t n, bool keyed_header, const Fields* f) {
    char buf[kNumberBufSize];
    o_.put('[');
    o_.put(std::string_view(buf, format_int64(n, buf)));
    if (keyed_header) o_.put(':');
    if (opts_.delim != ',') o_.put(opts_.delim);
    o_.put(']');
    if (f) field_list(*f, 0);
    o_.put(':');
}

void Encoder::field_list(const Fields& f, size_t gi) {
    o_.put('{');
    const auto& g = f.groups[gi];
    for (size_t j = 0; j < g.size(); j++) {
        if (j > 0) o_.put(opts_.delim);
        write_key(o_, char_utf8(g[j].name));
        if (g[j].group >= 0) field_list(f, static_cast<size_t>(g[j].group));
    }
    o_.put('}');
}

void Encoder::cells(Val obj, const Fields& f, size_t gi, bool& first) {
    SEXP names = obj_names(obj);
    const R_xlen_t n = obj_size(obj);
    const auto& g = f.groups[gi];
    for (size_t j = 0; j < g.size(); j++) {
        const Val v = obj_value(obj, find_name(names, n, g[j].name, static_cast<R_xlen_t>(j)));
        if (g[j].group >= 0) {
            cells(v, f, static_cast<size_t>(g[j].group), first);
        } else {
            if (!first) o_.put(opts_.delim);
            first = false;
            prim(v);
        }
    }
}

// A field of an object at `depth`. With `on_hyphen` it is the first field of
// a list-item object and shares the hyphen line one level up (spec 10).
void Encoder::field(SEXP key, Val v, size_t depth, bool on_hyphen) {
    if (on_hyphen) {
        line(depth - 1);
        o_.put("- ");
    } else {
        line(depth);
    }
    write_key(o_, char_utf8(key));
    switch (kind(v)) {
        case Kind::Prim:
            o_.put(": ");
            prim(v);
            break;
        case Kind::Object: {
            const R_xlen_t n = obj_size(v);
            if (n == 0) {
                o_.put(':');
                break;
            }
            check_unique(obj_names(v));
            Fields f;
            if (keyed(v, f)) {
                header(n, true, &f);
                keyed_rows(v, f, depth + 1);
            } else {
                o_.put(':');
                fields_of(v, depth + 1);
            }
            break;
        }
        case Kind::Array:
            array_after_key(v, depth + 1, true);
            break;
    }
}

void Encoder::fields_of(Val obj, size_t depth) {
    SEXP names = obj_names(obj);
    const R_xlen_t n = obj_size(obj);
    for (R_xlen_t j = 0; j < n; j++) {
        field(STRING_ELT(names, j), obj_value(obj, j), depth, false);
    }
}

void Encoder::keyed_rows(Val obj, const Fields& f, size_t depth) {
    SEXP names = obj_names(obj);
    const R_xlen_t n = obj_size(obj);
    for (R_xlen_t j = 0; j < n; j++) {
        line(depth);
        write_key(o_, char_utf8(STRING_ELT(names, j)));
        o_.put(": ");
        bool first = true;
        cells(obj_value(obj, j), f, 0, first);
    }
}

// The array part of a field (or of the root, without a key) whose content
// lines go at `depth`.
void Encoder::array_after_key(Val arr, size_t depth, bool has_key) {
    const R_xlen_t n = arr_size(arr);
    if (n == 0) {
        o_.put(has_key ? std::string_view(": []") : std::string_view("[]"));
        return;
    }
    Fields f;
    if (is_df(arr.x)) {
        std::vector<PrimVec> leaves;
        f.groups.assign(1, {});
        if (df_leaves(arr.x, f, 0, leaves)) {
            header(n, false, &f);
            for (R_xlen_t i = 0; i < n; i++) {
                line(depth);
                for (size_t k = 0; k < leaves.size(); k++) {
                    if (k > 0) o_.put(opts_.delim);
                    write_prim(o_, leaves[k], i, opts_.delim);
                }
                if ((i & 0xFFFF) == 0xFFFF) check_interrupt();
            }
            return;
        }
    }
    if (all_prim(arr)) {
        header(n, false, nullptr);
        o_.put(' ');
        for (R_xlen_t k = 0; k < n; k++) {
            if (k > 0) o_.put(opts_.delim);
            prim(arr_elem(arr, k));
        }
        return;
    }
    if (tabular(arr, f)) {
        header(n, false, &f);
        for (R_xlen_t k = 0; k < n; k++) {
            line(depth);
            bool first = true;
            cells(arr_elem(arr, k), f, 0, first);
            if ((k & 0xFFFF) == 0xFFFF) check_interrupt();
        }
        return;
    }
    header(n, false, nullptr);
    for (R_xlen_t k = 0; k < n; k++) item(arr_elem(arr, k), depth);
}

// A list item with its hyphen at `depth` (spec 9.4, 10).
void Encoder::item(Val v, size_t depth) {
    switch (kind(v)) {
        case Kind::Prim:
            line(depth);
            o_.put("- ");
            prim(v);
            break;
        case Kind::Array: {
            line(depth);
            o_.put("- ");
            const R_xlen_t n = arr_size(v);
            if (n > 0 && !all_prim(v)) {
                // Tabular form is not available here: list form only.
                header(n, false, nullptr);
                for (R_xlen_t k = 0; k < n; k++) item(arr_elem(v, k), depth + 1);
                break;
            }
            header(n, false, nullptr);
            if (n > 0) {
                o_.put(' ');
                for (R_xlen_t k = 0; k < n; k++) {
                    if (k > 0) o_.put(opts_.delim);
                    prim(arr_elem(v, k));
                }
            }
            break;
        }
        case Kind::Object: {
            const R_xlen_t n = obj_size(v);
            if (n == 0) {
                line(depth);
                o_.put('-');
                break;
            }
            SEXP names = obj_names(v);
            check_unique(names);
            for (R_xlen_t j = 0; j < n; j++) {
                field(STRING_ELT(names, j), obj_value(v, j), depth + 1, j == 0);
            }
            break;
        }
    }
}

void Encoder::document(SEXP x) {
    const Val v{x, -1};
    switch (kind(v)) {
        case Kind::Prim:
            line(0);
            prim(v, true);
            break;
        case Kind::Array:
            line(0);
            array_after_key(v, 1, false);
            break;
        case Kind::Object: {
            const R_xlen_t n = obj_size(v);
            if (n == 0) break;  // empty document
            check_unique(obj_names(v));
            Fields f;
            if (keyed(v, f)) {
                line(0);
                header(n, true, &f);
                keyed_rows(v, f, 1);
            } else {
                fields_of(v, 0);
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// StreamWriter
// ---------------------------------------------------------------------------

StreamWriter::StreamWriter(const std::string& path, const std::string& tmp_path,
                           std::vector<std::string> fields, const WriteOptions& opts)
    : path_(path), tmp_path_(tmp_path), fields_(std::move(fields)), opts_(opts) {
    tmp_ = std::fopen(tmp_path_.c_str(), "wb");
    if (!tmp_) {
        throw std::runtime_error("Cannot open temporary file for writing: " + tmp_path_);
    }
    out_ = Out(tmp_, tmp_path_);
    out_.set_started();  // every row line starts with a newline
}

StreamWriter::~StreamWriter() {
    if (!closed_) {
        if (tmp_) std::fclose(tmp_);
        std::remove(tmp_path_.c_str());
    }
}

void StreamWriter::write_batch(SEXP df) {
    if (closed_) throw std::runtime_error("Stream writer has been closed");
    const R_xlen_t ncol = Rf_xlength(df);
    if (static_cast<size_t>(ncol) != fields_.size()) {
        throw std::runtime_error("Batch has " + std::to_string(ncol) + " columns but the schema has " +
                                 std::to_string(fields_.size()));
    }
    std::vector<PrimVec> cols;
    cols.reserve(static_cast<size_t>(ncol));
    for (R_xlen_t j = 0; j < ncol; j++) {
        SEXP col = VECTOR_ELT(df, j);
        if (!is_atomic(col)) {
            throw std::runtime_error("Column '" + fields_[static_cast<size_t>(j)] +
                                     "' must be an atomic vector");
        }
        cols.push_back(prim_vec(col));
    }
    const R_xlen_t nrow = ncol > 0 ? Rf_xlength(VECTOR_ELT(df, 0)) : 0;
    const size_t spaces = static_cast<size_t>(opts_.indent);
    for (R_xlen_t i = 0; i < nrow; i++) {
        out_.line(spaces);
        for (size_t j = 0; j < cols.size(); j++) {
            if (j > 0) out_.put(opts_.delim);
            write_prim(out_, cols[j], i, opts_.delim);
        }
    }
    rows_ += static_cast<size_t>(nrow);
}

void StreamWriter::close(bool commit) {
    if (closed_) return;
    closed_ = true;
    bool ok = true;
    try {
        out_.flush();
    } catch (...) {
        ok = false;
    }
    if (std::fclose(tmp_) != 0) ok = false;
    tmp_ = nullptr;
    if (!commit || !ok) {
        std::remove(tmp_path_.c_str());
        if (!ok && commit) throw std::runtime_error("Error writing to file: " + tmp_path_);
        return;
    }

    Out head;
    if (rows_ == 0) {
        head.put("[]");
    } else {
        char buf[kNumberBufSize];
        head.put('[');
        head.put(std::string_view(buf, format_int64(static_cast<int64_t>(rows_), buf)));
        if (opts_.delim != ',') head.put(opts_.delim);
        head.put("]{");
        for (size_t j = 0; j < fields_.size(); j++) {
            if (j > 0) head.put(opts_.delim);
            write_key(head, fields_[j]);
        }
        head.put("}:");
    }

    std::FILE* out = std::fopen(path_.c_str(), "wb");
    std::FILE* in = out ? std::fopen(tmp_path_.c_str(), "rb") : nullptr;
    ok = out && in;
    if (ok) {
        const std::string& h = head.text();
        ok = std::fwrite(h.data(), 1, h.size(), out) == h.size();
        std::vector<char> chunk(Out::kChunk);
        size_t got;
        while (ok && (got = std::fread(chunk.data(), 1, chunk.size(), in)) > 0) {
            ok = std::fwrite(chunk.data(), 1, got, out) == got;
        }
        ok = ok && !std::ferror(in);
    }
    if (in) std::fclose(in);
    if (out && std::fclose(out) != 0) ok = false;
    std::remove(tmp_path_.c_str());
    if (!ok) throw std::runtime_error("Error writing to file: " + path_);
}

}  // namespace toonlite
