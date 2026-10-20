#include "toon_df.h"
#include "toon_number.h"

#include <algorithm>
#include <charconv>

namespace toonlite {

namespace {

// Never reserve in proportion to an attacker-controlled declared length
// before the rows exist (spec section 15).
constexpr size_t kMaxReserve = 1 << 16;

}  // namespace

// ---------------------------------------------------------------------------
// ColBuilder
// ---------------------------------------------------------------------------

ColBuilder::ColBuilder(std::string name, ColType forced)
    : name_(std::move(name)), forced_(forced), type_(forced) {}

void ColBuilder::reserve(size_t n) {
    reserve_ = n;
    switch (type_) {
        case ColType::LOGICAL:
        case ColType::INTEGER: ints_.reserve(n); break;
        case ColType::DOUBLE: dbls_.reserve(n); break;
        case ColType::STRING: strs_.reserve(n); break;
        case ColType::UNKNOWN: break;
    }
}

void ColBuilder::reset() {
    ints_.clear();
    dbls_.clear();
    strs_.clear();
    pool_.clear();
    n_ = 0;
    type_ = forced_;
}

// From UNKNOWN: the n_ values seen so far were all null.
void ColBuilder::set_type(ColType t) {
    type_ = t;
    switch (t) {
        case ColType::LOGICAL:
        case ColType::INTEGER:
            ints_.reserve(std::max(reserve_, n_));
            ints_.assign(n_, NA_INTEGER);
            break;
        case ColType::DOUBLE:
            dbls_.reserve(std::max(reserve_, n_));
            dbls_.assign(n_, NA_REAL);
            break;
        case ColType::STRING:
            strs_.reserve(std::max(reserve_, n_));
            strs_.assign(n_, StrRef{kNA, 0});
            break;
        case ColType::UNKNOWN:
            break;
    }
}

void ColBuilder::promote(ColType t) {
    if (type_ == t) return;
    if (type_ == ColType::UNKNOWN) {
        set_type(t);
        return;
    }
    char buf[kNumberBufSize];
    switch (t) {
        case ColType::INTEGER:
            // LOGICAL -> INTEGER: TRUE/FALSE are already 1/0 and NA_LOGICAL
            // equals NA_INTEGER.
            break;
        case ColType::DOUBLE:
            dbls_.reserve(std::max(reserve_, n_));
            dbls_.resize(n_);
            for (size_t i = 0; i < n_; i++) {
                dbls_[i] = ints_[i] == NA_INTEGER ? NA_REAL : static_cast<double>(ints_[i]);
            }
            ints_.clear();
            break;
        case ColType::STRING:
            strs_.reserve(std::max(reserve_, n_));
            strs_.clear();
            for (size_t i = 0; i < n_; i++) {
                std::string_view text;
                bool na = false;
                if (type_ == ColType::DOUBLE) {
                    const double d = dbls_[i];
                    if (ISNA(d)) {
                        na = true;
                    } else {
                        text = std::string_view(buf, format_double(d, buf));
                    }
                } else {
                    const int v = ints_[i];
                    if (v == NA_INTEGER) {
                        na = true;
                    } else if (type_ == ColType::LOGICAL) {
                        text = v ? "true" : "false";
                    } else {
                        text = std::string_view(buf, format_int64(v, buf));
                    }
                }
                if (na) {
                    strs_.push_back(StrRef{kNA, 0});
                } else {
                    strs_.push_back(StrRef{pool_.size(), text.size()});
                    pool_.append(text.data(), text.size());
                }
            }
            ints_.clear();
            dbls_.clear();
            break;
        default:
            break;
    }
    type_ = t;
}

void ColBuilder::push_string(std::string_view s) {
    strs_.push_back(StrRef{pool_.size(), s.size()});
    pool_.append(s.data(), s.size());
    n_++;
}

void ColBuilder::push_text_of_number(const Scalar& s) {
    char buf[kNumberBufSize];
    switch (s.kind) {
        case ScalarKind::Bool: push_string(s.b ? "true" : "false"); break;
        case ScalarKind::Int: push_string(std::string_view(buf, format_int64(s.i, buf))); break;
        case ScalarKind::Double:
            if (std::isfinite(s.d)) {
                push_string(std::string_view(buf, format_double(s.d, buf)));
            } else {
                push_string(s.d > 0 ? "Inf" : "-Inf");
            }
            break;
        default: break;
    }
}

void ColBuilder::push_null() {
    switch (type_) {
        case ColType::UNKNOWN: break;
        case ColType::LOGICAL:
        case ColType::INTEGER: ints_.push_back(NA_INTEGER); break;
        case ColType::DOUBLE: dbls_.push_back(NA_REAL); break;
        case ColType::STRING: strs_.push_back(StrRef{kNA, 0}); break;
    }
    n_++;
}

void ColBuilder::push(std::string_view token, std::string& scratch) {
    const Scalar s = parse_scalar(token, scratch);
    if (s.kind == ScalarKind::Null) {
        push_null();
        return;
    }
    if (forced_ == ColType::STRING) {
        // A column forced to character keeps the token text as written.
        push_string(s.kind == ScalarKind::String ? s.str : token);
        return;
    }
    switch (s.kind) {
        case ScalarKind::Bool:
            if (type_ == ColType::UNKNOWN) set_type(ColType::LOGICAL);
            switch (type_) {
                case ColType::LOGICAL:
                case ColType::INTEGER: ints_.push_back(s.b ? 1 : 0); n_++; break;
                case ColType::DOUBLE: dbls_.push_back(s.b ? 1.0 : 0.0); n_++; break;
                default: push_text_of_number(s); break;
            }
            break;
        case ScalarKind::Int:
            if (type_ == ColType::UNKNOWN || type_ == ColType::LOGICAL) promote(ColType::INTEGER);
            switch (type_) {
                case ColType::INTEGER: ints_.push_back(s.i); n_++; break;
                case ColType::DOUBLE: dbls_.push_back(static_cast<double>(s.i)); n_++; break;
                default: push_text_of_number(s); break;
            }
            break;
        case ScalarKind::Double:
            if (type_ != ColType::STRING) {
                promote(ColType::DOUBLE);
                dbls_.push_back(s.d);
                n_++;
            } else {
                push_text_of_number(s);
            }
            break;
        case ScalarKind::String:
            promote(ColType::STRING);
            push_string(s.str);
            break;
        case ScalarKind::Null:
            break;
    }
}

SEXP ColBuilder::finalize() const {
    const R_xlen_t n = static_cast<R_xlen_t>(n_);
    SEXP out;
    switch (type_) {
        case ColType::UNKNOWN: {
            out = PROTECT(Rf_allocVector(LGLSXP, n));
            int* p = LOGICAL(out);
            for (R_xlen_t i = 0; i < n; i++) p[i] = NA_LOGICAL;
            break;
        }
        case ColType::LOGICAL:
            out = PROTECT(Rf_allocVector(LGLSXP, n));
            if (n > 0) std::copy(ints_.begin(), ints_.end(), LOGICAL(out));
            break;
        case ColType::INTEGER:
            out = PROTECT(Rf_allocVector(INTSXP, n));
            if (n > 0) std::copy(ints_.begin(), ints_.end(), INTEGER(out));
            break;
        case ColType::DOUBLE:
            out = PROTECT(Rf_allocVector(REALSXP, n));
            if (n > 0) std::copy(dbls_.begin(), dbls_.end(), REAL(out));
            break;
        case ColType::STRING:
        default:
            out = PROTECT(Rf_allocVector(STRSXP, n));
            for (R_xlen_t i = 0; i < n; i++) {
                const StrRef& r = strs_[static_cast<size_t>(i)];
                SET_STRING_ELT(out, i,
                               r.off == kNA ? NA_STRING
                                            : mkchar_utf8(std::string_view(pool_.data() + r.off, r.len)));
            }
            break;
    }
    UNPROTECT(1);
    return out;
}

SEXP build_dataframe(const std::vector<ColBuilder>& columns, size_t nrow) {
    const R_xlen_t ncol = static_cast<R_xlen_t>(columns.size());
    SEXP df = PROTECT(Rf_allocVector(VECSXP, ncol));
    SEXP names = PROTECT(Rf_allocVector(STRSXP, ncol));
    for (R_xlen_t i = 0; i < ncol; i++) {
        SET_VECTOR_ELT(df, i, columns[static_cast<size_t>(i)].finalize());
        SET_STRING_ELT(names, i, mkchar_utf8(columns[static_cast<size_t>(i)].name()));
    }
    Rf_setAttrib(df, R_NamesSymbol, names);

    SEXP row_names = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(row_names)[0] = NA_INTEGER;
    INTEGER(row_names)[1] = -static_cast<int>(nrow);
    Rf_setAttrib(df, R_RowNamesSymbol, row_names);
    Rf_setAttrib(df, R_ClassSymbol, Rf_mkString("data.frame"));
    UNPROTECT(3);
    return df;
}

// ---------------------------------------------------------------------------
// TabularReader
// ---------------------------------------------------------------------------

TabularReader::TabularReader(const std::string& path, const TableReadOptions& opts)
    : path_(path), opts_(opts), src_(path), lines_(src_, opts.read, path) {
    if (src_.has_error()) {
        throw ParseError(src_.error_message(), 0, 0, "", path);
    }
    locate();
    setup_columns();
}

// Position the reader on the first row of the target table.
void TabularReader::locate() {
    const Line* L = lines_.peek();
    if (!opts_.has_key) {
        if (!L) {
            throw ParseError("No tabular array found: the document is empty", 0, 0, "", path_);
        }
        if (L->depth != 0) {
            lines_.fail("The first line of a document must not be indented", L);
        }
        if (L->content == "[]") {
            lines_.consume();
            empty_table_ = true;
            return;
        }
        const LineScan sc = scan_line(L->content);
        if (!is_header_line(sc)) {
            lines_.fail("The document root is not a tabular array; use `key` to select a field", L);
        }
        try {
            parse_header(L->content, sc, lines_.strict(), header_);
        } catch (const LexError& e) {
            lines_.fail(e.what(), L);
        }
        if (header_.has_key) {
            lines_.fail("The document root is not a tabular array; use `key` to select a field", L);
        }
    } else {
        bool found = false;
        bool first = true;
        while ((L = lines_.peek()) != nullptr) {
            if (L->depth > 0) {
                if (first) {
                    lines_.fail("The first line of a document must not be indented", L);
                }
                lines_.consume();  // content of a field we are not reading
                continue;
            }
            first = false;
            const LineScan sc = scan_line(L->content);
            std::string key;
            bool is_header = is_header_line(sc);
            try {
                if (is_header) {
                    parse_header(L->content, sc, lines_.strict(), header_);
                    if (!header_.has_key) {
                        lines_.fail("`key` requires the document root to be an object", L);
                    }
                    key = header_.key;
                } else if (sc.colon != npos) {
                    decode_key(trim_spaces(L->content.substr(0, sc.colon)), key);
                } else {
                    lines_.fail("`key` requires the document root to be an object", L);
                }
            } catch (const LexError& e) {
                lines_.fail(e.what(), L);
            }
            if (key == opts_.key) {
                found = true;
                if (!is_header) {
                    if (trim_spaces(L->content.substr(sc.colon + 1)) == "[]") {
                        lines_.consume();
                        empty_table_ = true;
                        return;
                    }
                    lines_.fail("Field '" + key + "' is not a tabular array", L);
                }
                break;
            }
            lines_.consume();
        }
        if (!found) {
            throw ParseError("Key not found: " + opts_.key, 0, 0, "", path_);
        }
    }

    // L is the header line of the target array.
    if (header_.keyed) {
        lines_.fail("The target is a keyed object, not a tabular array", L);
    }
    header_.line_no = L->no;
    if (!header_.has_fields) {
        // An empty array in legacy form ([0]:) reads as an empty table.
        if (header_.length == 0 && !header_.length_overflow && header_.inline_values.empty()) {
            lines_.consume();
            const Line* next = lines_.peek();
            if (next && next->depth > 0) {
                lines_.fail("The target array is not a tabular array", next);
            }
            empty_table_ = true;
            return;
        }
        lines_.fail("The target array is not a tabular array", L);
    }
    lines_.consume();
    rows_ = std::make_unique<RowScanner>(lines_, header_, 1, !opts_.ragged_expand);
}

void TabularReader::setup_columns() {
    if (empty_table_) return;
    const std::vector<ResolvedGroup> groups = resolve_fields(header_.fields);
    cell_col_.assign(header_.fields.leaf_count, -1);

    // Columns in resolved (depth-first) order, named by their dotted path.
    std::vector<std::pair<std::string, int>> leaves;
    struct Walk {
        static void run(const std::vector<ResolvedGroup>& g, int gi, const std::string& prefix,
                        std::vector<std::pair<std::string, int>>& out) {
            const ResolvedGroup& rg = g[static_cast<size_t>(gi)];
            for (size_t k = 0; k < rg.keys.size(); k++) {
                std::string name = prefix.empty() ? rg.keys[k] : prefix + "." + rg.keys[k];
                if (rg.group[k] >= 0) {
                    run(g, rg.group[k], name, out);
                } else {
                    out.emplace_back(std::move(name), rg.leaf[k]);
                }
            }
        }
    };
    if (!groups.empty()) Walk::run(groups, 0, "", leaves);

    const size_t hint = header_.length_overflow
                            ? kMaxReserve
                            : std::min<uint64_t>(header_.length, kMaxReserve);
    for (auto& leaf : leaves) {
        ColType forced = ColType::UNKNOWN;
        for (const auto& ct : opts_.col_types) {
            if (ct.first == leaf.first) {
                forced = ct.second;
                break;
            }
        }
        cell_col_[static_cast<size_t>(leaf.second)] = static_cast<int>(cols_.size());
        cols_.emplace_back(std::move(leaf.first), forced);
        cols_.back().reserve(hint);
    }
}

void TabularReader::add_extra_column(size_t cell_index) {
    if (extra_cols_ + 1 > opts_.max_extra_cols) {
        lines_.fail("max_extra_cols exceeded", rows_->line_no());
    }
    extra_cols_++;
    std::string name = "V" + std::to_string(cell_index + 1);
    ColType forced = ColType::UNKNOWN;
    for (const auto& ct : opts_.col_types) {
        if (ct.first == name) forced = ct.second;
    }
    ColBuilder col(std::move(name), forced);
    for (size_t r = 0; r < batch_rows_; r++) col.push_null();
    cell_col_.push_back(static_cast<int>(cols_.size()));
    cols_.push_back(std::move(col));
}

size_t TabularReader::read(size_t max_rows) {
    if (empty_table_ || exhausted_) return 0;
    size_t got = 0;
    while (max_rows == 0 || got < max_rows) {
        if (!rows_->next(cells_)) {
            exhausted_ = true;
            break;
        }
        const size_t nc = cells_.size();
        if (opts_.ragged_expand) {
            min_cells_ = std::min(min_cells_, nc);
            max_cells_ = std::max(max_cells_, nc);
            while (cell_col_.size() < nc) add_extra_column(cell_col_.size());
        }
        try {
            for (size_t j = 0; j < nc; j++) {
                const int c = cell_col_[j];
                if (c >= 0) {
                    cols_[static_cast<size_t>(c)].push(cells_[j], scratch_);
                } else {
                    parse_scalar(cells_[j], scratch_);  // validate dropped duplicates
                }
            }
        } catch (const LexError& e) {
            lines_.fail(e.what(), rows_->line_no());
        }
        if (nc < cell_col_.size()) {
            for (size_t j = nc; j < cell_col_.size(); j++) {
                const int c = cell_col_[j];
                if (c >= 0) cols_[static_cast<size_t>(c)].push_null();
            }
        }
        batch_rows_++;
        total_rows_++;
        got++;
        if ((total_rows_ & 0xFFFF) == 0) check_interrupt();
    }
    return got;
}

SEXP TabularReader::take_batch() {
    SEXP df = build_dataframe(cols_, batch_rows_);
    for (auto& c : cols_) c.reset();
    batch_rows_ = 0;
    return df;
}

void TabularReader::finish(std::vector<std::string>& warnings) {
    if (!empty_table_) {
        if (!header_.count_matches(total_rows_)) {
            const std::string msg = count_mismatch_message(header_, total_rows_, "row");
            if (lines_.strict() && !opts_.n_mismatch_warn) {
                lines_.fail(msg, header_.line_no);
            }
            if (opts_.warn) warnings.push_back(msg + "; using the rows found.");
        }
        if (opts_.ragged_expand && opts_.warn && total_rows_ > 0 && min_cells_ != max_cells_) {
            std::string msg = "Tabular rows had inconsistent field counts (min=" +
                              std::to_string(min_cells_) + ", max=" + std::to_string(max_cells_) + ").";
            if (extra_cols_ > 0) {
                msg += " Schema expanded to " + std::to_string(cols_.size()) + " columns;";
            }
            msg += " missing values filled with NA.";
            warnings.push_back(msg);
        }
    }
    if (!opts_.has_key) {
        const Line* L = lines_.peek();
        if (L) lines_.fail("Unexpected content after the root array", L);
    }
}

}  // namespace toonlite
