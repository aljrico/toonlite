// .Call entry points. Every entry runs its body through guarded(), which
// turns C++ exceptions and R jumps from callbacks into R conditions only
// after all C++ frames are gone, and emits collected warnings on success.

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
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

// The UTF-8 text of a character scalar.
std::string_view text_arg(SEXP x) {
    SEXP c = STRING_ELT(x, 0);
    if (c == NA_STRING) throw std::invalid_argument("`text` must not be NA");
    if (Rf_getCharCE(c) == CE_BYTES) {
        throw std::invalid_argument("Strings marked as \"bytes\" cannot be decoded; convert them to UTF-8 first");
    }
    return Rf_translateCharUTF8(c);
}

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

SEXP C_from_toon(SEXP text, SEXP strict, SEXP simplify, SEXP indent) {
    return guarded("Error parsing TOON", [&](Warnings& w) {
        ReadOptions opts = read_options(strict, indent);
        opts.warnings = &w;
        Document doc;
        if (TYPEOF(text) == RAWSXP) {
            doc = parse_toon(reinterpret_cast<const char*>(RAW(text)),
                             static_cast<size_t>(Rf_xlength(text)), opts);
        } else {
            const std::string_view s = text_arg(text);
            doc = parse_toon(s.data(), s.size(), opts);
        }
        return document_to_r(doc, flag_arg(simplify));
    });
}

SEXP C_read_toon(SEXP file, SEXP strict, SEXP simplify, SEXP indent) {
    return guarded("Error reading TOON file", [&](Warnings& w) {
        ReadOptions opts = read_options(strict, indent);
        opts.warnings = &w;
        const Document doc = parse_toon_file(path_arg(file), opts);
        return document_to_r(doc, flag_arg(simplify));
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

SEXP C_validate_toon(SEXP x, SEXP is_file, SEXP strict, SEXP indent) {
    return guarded("Error validating TOON", [&](Warnings&) {
        const ReadOptions opts = read_options(strict, indent);
        ValidationResult vr = ValidationResult::ok();
        try {
            if (flag_arg(is_file)) {
                parse_toon_file(path_arg(x), opts);
            } else {
                const std::string_view s = text_arg(x);
                parse_toon(s.data(), s.size(), opts);
            }
        } catch (const ParseError& e) {
            vr = ValidationResult::error(e.what(), e.line(), e.column(), e.snippet(), e.file());
        }
        // A fresh vector: Rf_ScalarLogical() returns R's shared TRUE/FALSE,
        // which must never carry attributes.
        SEXP result = PROTECT(Rf_allocVector(LGLSXP, 1));
        LOGICAL(result)[0] = vr.valid ? TRUE : FALSE;
        if (!vr.valid) {
            NamedList info(6);
            info.set(0, "type", Rf_mkString("parse_error"))
                .set(1, "message", Rf_ScalarString(mkchar_utf8(vr.message)))
                .set(2, "line", Rf_ScalarInteger(vr.line > 0 && vr.line <= INT_MAX ? static_cast<int>(vr.line) : NA_INTEGER))
                .set(3, "column", Rf_ScalarInteger(vr.column > 0 && vr.column <= INT_MAX ? static_cast<int>(vr.column) : NA_INTEGER))
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

// Stream the items of a list array to an R callback in batches
SEXP C_stream_items(SEXP file, SEXP key, SEXP callback, SEXP batch_size, SEXP strict,
                    SEXP indent, SEXP warn, SEXP simplify) {
    return guarded("Error streaming TOON", [&](Warnings& w) {
        ItemStream s;
        if (key != R_NilValue) {
            s.has_key = true;
            s.key = Rf_translateCharUTF8(STRING_ELT(key, 0));
        }
        const int bs = Rf_asInteger(batch_size);
        s.batch_size = (bs == NA_INTEGER || bs < 1) ? 1 : static_cast<size_t>(bs);
        s.simplify = flag_arg(simplify);
        s.callback = callback;
        ReadOptions opts = read_options(strict, indent);
        if (flag_arg(warn)) opts.warnings = &w;
        stream_toon_items(path_arg(file), opts, s);
        return R_NilValue;
    });
}

SEXP C_toon_peek(SEXP file, SEXP n) {
    return guarded("Error peeking TOON", [&](Warnings&) {
        const int max_lines = Rf_asInteger(n);
        const PeekInfo p = peek_toon_file(
            path_arg(file), max_lines == NA_INTEGER || max_lines < 1 ? 1 : static_cast<size_t>(max_lines));
        NamedList out(3);
        out.set(0, "type", Rf_mkString(p.type.c_str()))
            .set(1, "first_keys", strings_to_sexp(p.first_keys))
            .set(2, "preview", strings_to_sexp(p.preview));
        return out.done();
    });
}

SEXP C_toon_info(SEXP file, SEXP strict, SEXP indent) {
    return guarded("Error getting TOON info", [&](Warnings&) {
        const DocumentInfo info =
            document_info(parse_toon_file(path_arg(file), read_options(strict, indent)));
        auto count = [](size_t v) {
            return v <= static_cast<size_t>(INT_MAX) ? Rf_ScalarInteger(static_cast<int>(v))
                                                     : Rf_ScalarReal(static_cast<double>(v));
        };
        NamedList out(4);
        out.set(0, "array_count", count(info.arrays))
            .set(1, "object_count", count(info.objects))
            .set(2, "has_tabular", Rf_ScalarLogical(info.has_tabular ? TRUE : FALSE))
            .set(3, "declared_rows",
                 info.has_tabular ? count(info.first_table_rows) : Rf_ScalarInteger(NA_INTEGER));
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
