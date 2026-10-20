// .Call entry points. Every entry runs its body through guarded(), which
// turns C++ exceptions and R jumps from callbacks into R conditions only
// after all C++ frames are gone, and emits collected warnings on success.

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "toon_df.h"
#include "toon_encoder.h"
#include "toon_errors.h"
#include "toon_parser.h"
#include "toon_r.h"

using namespace toonlite;

namespace {

using Warnings = std::vector<std::string>;

constexpr size_t kErrorBufSize = 8192;

void copy_message(char* buf, const std::string& msg) {
    const size_t n = std::min(msg.size(), kErrorBufSize - 1);
    std::memcpy(buf, msg.data(), n);
    buf[n] = '\0';
}

SEXP strings_to_sexp(const std::vector<std::string>& v) {
    SEXP out = PROTECT(Rf_allocVector(STRSXP, static_cast<R_xlen_t>(v.size())));
    for (size_t i = 0; i < v.size(); i++) {
        SET_STRING_ELT(out, static_cast<R_xlen_t>(i), mkchar_utf8(v[i]));
    }
    UNPROTECT(1);
    return out;
}

// Run `body(warnings)`; see the comment at the top of the file.
template <typename F>
SEXP guarded(const char* context, F&& body) {
    char msg[kErrorBufSize];
    msg[0] = '\0';
    SEXP token = nullptr;
    bool failed = false;
    SEXP result = R_NilValue;
    SEXP warns = R_NilValue;
    try {
        Warnings w;
        result = PROTECT(body(w));
        warns = PROTECT(strings_to_sexp(w));
    } catch (const UnwindException& e) {
        token = e.token;
    } catch (const ParseError& e) {
        copy_message(msg, e.formatted_message());
        failed = true;
    } catch (const std::exception& e) {
        copy_message(msg, std::string(context) + ": " + e.what());
        failed = true;
    } catch (...) {
        copy_message(msg, std::string(context) + ": unknown error");
        failed = true;
    }
    if (token) R_ContinueUnwind(token);
    if (failed) Rf_error("%s", msg);
    for (R_xlen_t i = 0; i < Rf_xlength(warns); i++) {
        Rf_warning("%s", Rf_translateChar(STRING_ELT(warns, i)));
    }
    UNPROTECT(2);
    return result;
}

std::string path_arg(SEXP x) {
    return std::string(R_ExpandFileName(Rf_translateChar(STRING_ELT(x, 0))));
}

bool flag_arg(SEXP x) { return Rf_asLogical(x) == TRUE; }

ReadOptions read_options(SEXP strict, SEXP indent) {
    ReadOptions o;
    o.strict = flag_arg(strict);
    const int n = Rf_asInteger(indent);
    if (n == NA_INTEGER || n < 1) {
        throw std::invalid_argument("`indent` must be a positive integer");
    }
    o.indent = n;
    return o;
}

WriteOptions write_options(SEXP indent, SEXP delimiter) {
    WriteOptions o;
    const int n = Rf_asInteger(indent);
    if (n == NA_INTEGER || n < 1) {
        throw std::invalid_argument("`indent` must be a positive integer");
    }
    o.indent = n;
    const char* d = CHAR(STRING_ELT(delimiter, 0));
    if (std::strcmp(d, ",") != 0 && std::strcmp(d, "\t") != 0 && std::strcmp(d, "|") != 0) {
        throw std::invalid_argument("`delimiter` must be \",\", \"\\t\" or \"|\"");
    }
    o.delim = d[0];
    return o;
}

// An output file that is removed again unless commit() succeeds.
class OutputFile {
public:
    explicit OutputFile(std::string path) : path_(std::move(path)) {
        f_ = std::fopen(path_.c_str(), "wb");
        if (!f_) throw std::runtime_error("Cannot open file for writing: " + path_);
    }
    ~OutputFile() {
        if (f_) {
            std::fclose(f_);
            std::remove(path_.c_str());
        }
    }
    std::FILE* get() const { return f_; }
    const std::string& path() const { return path_; }
    void commit() {
        std::FILE* f = f_;
        f_ = nullptr;
        if (std::fclose(f) != 0) {
            std::remove(path_.c_str());
            throw std::runtime_error("Error writing to file: " + path_);
        }
    }

private:
    std::string path_;
    std::FILE* f_ = nullptr;
};

std::vector<std::pair<std::string, ColType>> parse_col_types(SEXP col_types) {
    std::vector<std::pair<std::string, ColType>> out;
    if (col_types == R_NilValue) return out;
    SEXP names = Rf_getAttrib(col_types, R_NamesSymbol);
    for (R_xlen_t i = 0; i < Rf_xlength(col_types); i++) {
        const std::string type(Rf_translateCharUTF8(STRING_ELT(col_types, i)));
        ColType t = ColType::STRING;
        if (type == "logical") {
            t = ColType::LOGICAL;
        } else if (type == "integer") {
            t = ColType::INTEGER;
        } else if (type == "double") {
            t = ColType::DOUBLE;
        }
        out.emplace_back(Rf_translateCharUTF8(STRING_ELT(names, i)), t);
    }
    return out;
}

TableReadOptions table_options(SEXP key, SEXP strict, SEXP indent, SEXP warn,
                               SEXP col_types, SEXP ragged_rows, SEXP n_mismatch,
                               SEXP max_extra_cols) {
    TableReadOptions o;
    o.read = read_options(strict, indent);
    o.warn = flag_arg(warn);
    if (key != R_NilValue) {
        o.has_key = true;
        o.key = Rf_translateCharUTF8(STRING_ELT(key, 0));
    }
    o.ragged_expand = std::strcmp(CHAR(STRING_ELT(ragged_rows, 0)), "expand_warn") == 0;
    o.n_mismatch_warn = std::strcmp(CHAR(STRING_ELT(n_mismatch, 0)), "warn") == 0;
    const double m = Rf_asReal(max_extra_cols);
    o.max_extra_cols = (std::isinf(m) || m >= 1e15) ? SIZE_MAX : static_cast<size_t>(m < 0 ? 0 : m);
    o.col_types = parse_col_types(col_types);
    return o;
}

}  // namespace

// Helper to convert Node to SEXP
static SEXP node_to_sexp(const NodePtr& node, bool simplify) {
    if (!node) return R_NilValue;

    switch (node->kind) {
        case NodeKind::N_NULL:
            return R_NilValue;

        case NodeKind::N_BOOL: {
            SEXP result = PROTECT(Rf_allocVector(LGLSXP, 1));
            LOGICAL(result)[0] = node->bool_val ? TRUE : FALSE;
            UNPROTECT(1);
            return result;
        }

        case NodeKind::N_INT: {
            SEXP result = PROTECT(Rf_allocVector(INTSXP, 1));
            INTEGER(result)[0] = static_cast<int>(node->int_val);
            UNPROTECT(1);
            return result;
        }

        case NodeKind::N_DOUBLE: {
            SEXP result = PROTECT(Rf_allocVector(REALSXP, 1));
            REAL(result)[0] = node->double_val;
            UNPROTECT(1);
            return result;
        }

        case NodeKind::N_STRING: {
            SEXP result = PROTECT(Rf_allocVector(STRSXP, 1));
            SET_STRING_ELT(result, 0, Rf_mkCharCE(node->string_val.c_str(), CE_UTF8));
            UNPROTECT(1);
            return result;
        }

        case NodeKind::N_ARRAY: {
            size_t n = node->array_items.size();

            if (simplify && n > 0) {
                // Check if all items are same primitive type
                NodeKind first_kind = node->array_items[0]->kind;
                bool all_same = true;
                bool all_primitive = true;

                for (const auto& item : node->array_items) {
                    if (item->kind == NodeKind::N_ARRAY || item->kind == NodeKind::N_OBJECT) {
                        all_primitive = false;
                        break;
                    }
                    if (item->kind != first_kind && item->kind != NodeKind::N_NULL) {
                        if (first_kind == NodeKind::N_NULL) {
                            first_kind = item->kind;
                        } else {
                            all_same = false;
                        }
                    }
                }

                if (all_primitive && all_same) {
                    // Convert to atomic vector
                    switch (first_kind) {
                        case NodeKind::N_BOOL: {
                            SEXP result = PROTECT(Rf_allocVector(LGLSXP, n));
                            int* data = LOGICAL(result);
                            for (size_t i = 0; i < n; i++) {
                                if (node->array_items[i]->kind == NodeKind::N_NULL) {
                                    data[i] = NA_LOGICAL;
                                } else {
                                    data[i] = node->array_items[i]->bool_val ? TRUE : FALSE;
                                }
                            }
                            UNPROTECT(1);
                            return result;
                        }
                        case NodeKind::N_INT: {
                            SEXP result = PROTECT(Rf_allocVector(INTSXP, n));
                            int* data = INTEGER(result);
                            for (size_t i = 0; i < n; i++) {
                                if (node->array_items[i]->kind == NodeKind::N_NULL) {
                                    data[i] = NA_INTEGER;
                                } else {
                                    data[i] = static_cast<int>(node->array_items[i]->int_val);
                                }
                            }
                            UNPROTECT(1);
                            return result;
                        }
                        case NodeKind::N_DOUBLE: {
                            SEXP result = PROTECT(Rf_allocVector(REALSXP, n));
                            double* data = REAL(result);
                            for (size_t i = 0; i < n; i++) {
                                if (node->array_items[i]->kind == NodeKind::N_NULL) {
                                    data[i] = NA_REAL;
                                } else {
                                    data[i] = node->array_items[i]->double_val;
                                }
                            }
                            UNPROTECT(1);
                            return result;
                        }
                        case NodeKind::N_STRING: {
                            SEXP result = PROTECT(Rf_allocVector(STRSXP, n));
                            for (size_t i = 0; i < n; i++) {
                                if (node->array_items[i]->kind == NodeKind::N_NULL) {
                                    SET_STRING_ELT(result, i, NA_STRING);
                                } else {
                                    SET_STRING_ELT(result, i,
                                        Rf_mkCharCE(node->array_items[i]->string_val.c_str(), CE_UTF8));
                                }
                            }
                            UNPROTECT(1);
                            return result;
                        }
                        default:
                            break;
                    }
                }
            }

            // Return as list
            SEXP result = PROTECT(Rf_allocVector(VECSXP, n));
            for (size_t i = 0; i < n; i++) {
                SET_VECTOR_ELT(result, i, node_to_sexp(node->array_items[i], simplify));
            }
            UNPROTECT(1);
            return result;
        }

        case NodeKind::N_OBJECT: {
            size_t n = node->object_items.size();
            SEXP result = PROTECT(Rf_allocVector(VECSXP, n));
            SEXP names = PROTECT(Rf_allocVector(STRSXP, n));

            for (size_t i = 0; i < n; i++) {
                SET_STRING_ELT(names, i, Rf_mkCharCE(node->object_items[i].first.c_str(), CE_UTF8));
                SET_VECTOR_ELT(result, i, node_to_sexp(node->object_items[i].second, simplify));
            }

            Rf_setAttrib(result, R_NamesSymbol, names);
            UNPROTECT(2);
            return result;
        }
    }

    return R_NilValue;
}

static void add_warnings(const std::vector<Warning>& from, Warnings& to) {
    for (const auto& w : from) to.push_back(w.message);
}

// A named list filled one element at a time, so every value is reachable
// before the next allocation. Leaves one PROTECT on the stack until done().
class NamedList {
public:
    explicit NamedList(R_xlen_t n) {
        list_ = PROTECT(Rf_allocVector(VECSXP, n));
        names_ = Rf_allocVector(STRSXP, n);
        Rf_setAttrib(list_, R_NamesSymbol, names_);
    }
    NamedList& set(R_xlen_t i, const char* name, SEXP value) {
        SET_VECTOR_ELT(list_, i, value);
        SET_STRING_ELT(names_, i, Rf_mkChar(name));
        return *this;
    }
    SEXP done() {
        UNPROTECT(1);
        return list_;
    }

private:
    SEXP list_;
    SEXP names_;
};

static SEXP string_or_na(const std::string& s) {
    return Rf_ScalarString(s.empty() ? NA_STRING : mkchar_utf8(s));
}

static SEXP toon_string(const std::string& text, bool classed) {
    SEXP out = PROTECT(Rf_ScalarString(mkchar_utf8(text)));
    if (classed) Rf_setAttrib(out, R_ClassSymbol, Rf_mkString("toon"));
    UNPROTECT(1);
    return out;
}

static StreamWriter* stream_writer(SEXP ptr) {
    auto* w = static_cast<StreamWriter*>(R_ExternalPtrAddr(ptr));
    if (!w) throw std::runtime_error("Stream writer has been closed");
    return w;
}

static void finalize_stream_writer(SEXP p) {
    auto* w = static_cast<StreamWriter*>(R_ExternalPtrAddr(p));
    if (w) {
        delete w;
        R_ClearExternalPtr(p);
    }
}

extern "C" {

SEXP C_from_toon(SEXP text, SEXP strict, SEXP simplify, SEXP allow_comments,
                 SEXP allow_duplicate_keys) {
    return guarded("Error parsing TOON", [&](Warnings& w) {
        ParseOptions opts;
        opts.strict = flag_arg(strict);
        opts.simplify = flag_arg(simplify);
        opts.allow_comments = flag_arg(allow_comments);
        opts.allow_duplicate_keys = flag_arg(allow_duplicate_keys);
        Parser parser(opts);
        NodePtr node;
        if (TYPEOF(text) == RAWSXP) {
            node = parser.parse_string(reinterpret_cast<const char*>(RAW(text)),
                                       static_cast<size_t>(Rf_xlength(text)));
        } else {
            const char* data = Rf_translateCharUTF8(STRING_ELT(text, 0));
            node = parser.parse_string(data, std::strlen(data));
        }
        add_warnings(parser.warnings(), w);
        return node_to_sexp(node, opts.simplify);
    });
}

SEXP C_read_toon(SEXP file, SEXP strict, SEXP simplify, SEXP allow_comments,
                 SEXP allow_duplicate_keys) {
    return guarded("Error reading TOON file", [&](Warnings& w) {
        ParseOptions opts;
        opts.strict = flag_arg(strict);
        opts.simplify = flag_arg(simplify);
        opts.allow_comments = flag_arg(allow_comments);
        opts.allow_duplicate_keys = flag_arg(allow_duplicate_keys);
        Parser parser(opts);
        NodePtr node = parser.parse_file(path_arg(file));
        add_warnings(parser.warnings(), w);
        return node_to_sexp(node, opts.simplify);
    });
}

SEXP C_to_toon(SEXP x, SEXP indent, SEXP delimiter) {
    return guarded("Error encoding to TOON", [&](Warnings&) {
        Out out;
        Encoder(out, write_options(indent, delimiter)).document(x);
        if (out.text().size() > static_cast<size_t>(INT_MAX)) {
            throw std::runtime_error("The TOON text is larger than an R string can hold (2^31 - 1 bytes); use write_toon()");
        }
        return toon_string(out.text(), true);
    });
}

SEXP C_write_toon(SEXP x, SEXP file, SEXP indent, SEXP delimiter) {
    return guarded("Error writing TOON", [&](Warnings&) {
        const WriteOptions opts = write_options(indent, delimiter);
        OutputFile f(path_arg(file));
        Out out(f.get(), f.path());
        Encoder(out, opts).document(x);
        out.flush();
        f.commit();
        return R_NilValue;
    });
}

SEXP C_validate_toon(SEXP x, SEXP is_file, SEXP strict, SEXP allow_comments,
                     SEXP allow_duplicate_keys) {
    return guarded("Error validating TOON", [&](Warnings&) {
        ParseOptions opts;
        opts.strict = flag_arg(strict);
        opts.allow_comments = flag_arg(allow_comments);
        opts.allow_duplicate_keys = flag_arg(allow_duplicate_keys);
        Parser parser(opts);
        const ValidationResult vr = flag_arg(is_file)
                                        ? parser.validate_file(path_arg(x))
                                        : parser.validate_string(Rf_translateCharUTF8(STRING_ELT(x, 0)));
        SEXP result = PROTECT(Rf_ScalarLogical(vr.valid ? TRUE : FALSE));
        if (!vr.valid) {
            NamedList info(6);
            info.set(0, "type", Rf_mkString("parse_error"))
                .set(1, "message", Rf_ScalarString(mkchar_utf8(vr.message)))
                .set(2, "line", Rf_ScalarInteger(vr.line > 0 ? static_cast<int>(vr.line) : NA_INTEGER))
                .set(3, "column", Rf_ScalarInteger(vr.column > 0 ? static_cast<int>(vr.column) : NA_INTEGER))
                .set(4, "snippet", string_or_na(vr.snippet))
                .set(5, "file", string_or_na(vr.file));
            Rf_setAttrib(result, Rf_install("error"), info.done());
        }
        UNPROTECT(1);
        return result;
    });
}

SEXP C_read_toon_df(SEXP file, SEXP key, SEXP strict, SEXP indent, SEXP warn,
                    SEXP col_types, SEXP ragged_rows, SEXP n_mismatch,
                    SEXP max_extra_cols) {
    return guarded("Error reading tabular TOON", [&](Warnings& w) {
        TabularReader reader(path_arg(file),
                             table_options(key, strict, indent, warn, col_types,
                                           ragged_rows, n_mismatch, max_extra_cols));
        reader.read(0);
        reader.finish(w);
        return reader.take_batch();
    });
}

// Stream tabular rows to an R callback in batches
SEXP C_stream_rows(SEXP file, SEXP key, SEXP callback, SEXP batch_size,
                   SEXP strict, SEXP indent, SEXP warn, SEXP col_types,
                   SEXP ragged_rows, SEXP n_mismatch, SEXP max_extra_cols) {
    return guarded("Error streaming TOON", [&](Warnings& w) {
        TabularReader reader(path_arg(file),
                             table_options(key, strict, indent, warn, col_types,
                                           ragged_rows, n_mismatch, max_extra_cols));
        const int bs = Rf_asInteger(batch_size);
        const size_t n = (bs == NA_INTEGER || bs < 1) ? 1 : static_cast<size_t>(bs);
        while (reader.read(n) > 0) {
            SEXP batch = PROTECT(reader.take_batch());
            call_r_function(callback, batch);
            UNPROTECT(1);
        }
        reader.finish(w);
        return R_NilValue;
    });
}

SEXP C_toon_peek(SEXP file, SEXP n, SEXP allow_comments) {
    (void)allow_comments;
    return guarded("Error peeking TOON", [&](Warnings&) {
        const std::string path = path_arg(file);
        const int max_lines = Rf_asInteger(n);
        BufferedReader reader(path);
        if (reader.has_error()) throw std::runtime_error(reader.error_message());

        std::vector<std::string> lines;
        std::string_view line;
        size_t line_no;
        std::string top_type = "unknown";
        std::vector<std::string> first_keys;
        while (static_cast<int>(lines.size()) < max_lines && reader.next_line(line, line_no)) {
            lines.emplace_back(line);
            size_t ind = 0;
            while (ind < line.size() && (line[ind] == ' ' || line[ind] == '\t')) ind++;
            if (ind == line.size() || line[ind] == '#') continue;
            const char c = line[ind];
            if (top_type == "unknown") {
                if (c == '[') {
                    top_type = line.find('{') != std::string_view::npos ? "tabular_array" : "array";
                } else if (c == '-') {
                    top_type = "array";
                } else if (line.find(':') != std::string_view::npos) {
                    top_type = "object";
                }
            }
            if (top_type == "object" && ind == 0 && first_keys.size() < 5) {
                size_t colon = line.find(':');
                if (colon != std::string_view::npos) {
                    std::string key(line.substr(0, colon));
                    const size_t lb = key.find('[');
                    if (lb != std::string::npos) key.resize(lb);
                    while (!key.empty() && key.back() == ' ') key.pop_back();
                    if (!key.empty()) first_keys.push_back(key);
                }
            }
        }

        NamedList out(3);
        out.set(0, "type", Rf_mkString(top_type.c_str()));
        out.set(1, "first_keys", strings_to_sexp(first_keys));
        out.set(2, "preview", strings_to_sexp(lines));
        return out.done();
    });
}

SEXP C_toon_info(SEXP file, SEXP allow_comments) {
    return guarded("Error getting TOON info", [&](Warnings&) {
        ParseOptions opts;
        opts.allow_comments = flag_arg(allow_comments);
        opts.simplify = false;
        Parser parser(opts);
        NodePtr node = parser.parse_file(path_arg(file));

        int arrays = 0, objects = 0;
        bool has_tabular = false;
        size_t declared_rows = 0;
        std::function<void(const NodePtr&)> count = [&](const NodePtr& n) {
            if (!n) return;
            if (n->kind == NodeKind::N_ARRAY) {
                arrays++;
                if (!n->array_items.empty()) {
                    bool all_objects = true;
                    for (const auto& item : n->array_items) {
                        if (item->kind != NodeKind::N_OBJECT) {
                            all_objects = false;
                            break;
                        }
                    }
                    if (all_objects) {
                        has_tabular = true;
                        declared_rows = n->array_items.size();
                    }
                }
                for (const auto& item : n->array_items) count(item);
            } else if (n->kind == NodeKind::N_OBJECT) {
                objects++;
                for (const auto& item : n->object_items) count(item.second);
            }
        };
        count(node);
        NamedList out(4);
        out.set(0, "array_count", Rf_ScalarInteger(arrays))
            .set(1, "object_count", Rf_ScalarInteger(objects))
            .set(2, "has_tabular", Rf_ScalarLogical(has_tabular ? TRUE : FALSE))
            .set(3, "declared_rows",
                 Rf_ScalarInteger(has_tabular ? static_cast<int>(declared_rows) : NA_INTEGER));
        return out.done();
    });
}

SEXP C_stream_write_init(SEXP file, SEXP tmp_file, SEXP schema, SEXP indent, SEXP delimiter) {
    return guarded("Error initializing stream writer", [&](Warnings&) {
        std::vector<std::string> fields;
        for (R_xlen_t i = 0; i < Rf_xlength(schema); i++) {
            fields.emplace_back(char_utf8(STRING_ELT(schema, i)));
        }
        auto writer = std::make_unique<StreamWriter>(path_arg(file), path_arg(tmp_file),
                                                     std::move(fields), write_options(indent, delimiter));
        SEXP ptr = PROTECT(R_MakeExternalPtr(writer.get(), R_NilValue, R_NilValue));
        writer.release();
        R_RegisterCFinalizerEx(ptr, finalize_stream_writer, TRUE);
        UNPROTECT(1);
        return ptr;
    });
}

SEXP C_stream_write_batch(SEXP ptr, SEXP df_batch) {
    return guarded("Error writing batch", [&](Warnings&) {
        stream_writer(ptr)->write_batch(df_batch);
        return R_NilValue;
    });
}

// Returns the number of rows written.
SEXP C_stream_write_close(SEXP ptr, SEXP commit) {
    return guarded("Error closing stream writer", [&](Warnings&) {
        auto* w = static_cast<StreamWriter*>(R_ExternalPtrAddr(ptr));
        if (!w) return Rf_ScalarReal(0);
        R_ClearExternalPtr(ptr);
        std::unique_ptr<StreamWriter> owned(w);
        owned->close(flag_arg(commit));
        return Rf_ScalarReal(static_cast<double>(owned->rows()));
    });
}

}  // extern "C"
