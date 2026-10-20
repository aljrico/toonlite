#ifndef TOON_ENCODER_H
#define TOON_ENCODER_H

// TOON encoder (spec v4.4 sections 2, 3, 7-12). One code path writes every
// value, so to_toon(), write_toon(), write_toon_df() and the stream writer
// produce identical text for identical data.
//
// R values map to the JSON model as follows:
//   NULL, NA, NaN, Inf             -> null
//   length-1 atomic vector         -> primitive (wrap in I() for an array)
//   other atomic vectors           -> array of primitives (names dropped)
//   list with a names attribute    -> object (duplicate names are an error)
//   list without names             -> array
//   data.frame                     -> array of row objects (tabular form)
//   factor                         -> its level as a string
//   Date / POSIXct                 -> "YYYY-MM-DD" / "YYYY-MM-DDTHH:MM:SSZ"
//   bit64::integer64               -> integer
//   anything else                  -> null

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "toon_r.h"

namespace toonlite {

struct WriteOptions {
    int indent = 2;    // indentSize
    char delim = ',';  // document delimiter: ',', '\t' or '|'
};

// Output text. With a file, the buffer is written out in chunks of about
// kChunk bytes at line boundaries, so memory stays flat for large outputs.
class Out {
public:
    static constexpr size_t kChunk = 1 << 20;

    Out() = default;
    explicit Out(std::FILE* f, const std::string& path) : file_(f), path_(path) {
        buf_.reserve(kChunk + kChunk / 4);
    }

    void put(char c) { buf_.push_back(c); }
    void put(std::string_view s) { buf_.append(s.data(), s.size()); }

    // Start a new line indented by `spaces`; no newline before the first
    // line, so documents never end with a newline (spec 12).
    void line(size_t spaces) {
        if (started_) {
            buf_.push_back('\n');
        } else {
            started_ = true;
        }
        buf_.append(spaces, ' ');
        if (file_ && buf_.size() >= kChunk) flush();
    }
    void set_started() { started_ = true; }

    void flush();
    std::string& text() { return buf_; }

private:
    std::string buf_;
    std::FILE* file_ = nullptr;
    std::string path_;
    bool started_ = false;
};

// One atomic vector, classified once so cells can be written in a loop.
enum class PrimKind : uint8_t {
    Null, Logical, Integer, Double, Int64, String, Factor, Date, DateTime
};

struct PrimVec {
    PrimKind kind = PrimKind::Null;
    SEXP x = R_NilValue;
    SEXP levels = R_NilValue;
    const int* ip = nullptr;
    const double* dp = nullptr;
};

PrimVec prim_vec(SEXP x);

// Element i of `v` as a TOON primitive, quoted against `delim`. `root`
// applies the root-only quoting rule (leading U+FEFF).
void write_prim(Out& o, const PrimVec& v, R_xlen_t i, char delim, bool root = false);

// A key or field name (spec 7.3).
void write_key(Out& o, std::string_view key);

// UTF-8 text of a CHARSXP; throws for "bytes" strings and invalid UTF-8.
std::string_view char_utf8(SEXP c);

class Encoder {
public:
    Encoder(Out& out, const WriteOptions& opts) : o_(out), opts_(opts) {}

    // Encode `x` as a whole document.
    void document(SEXP x);

private:
    struct Val {
        SEXP x;
        R_xlen_t i;  // >= 0: element (atomic) or row (data.frame) i of x
    };
    enum class Kind { Prim, Object, Array };

    // Tabular field lists: groups[0] is the top level; a field with
    // group >= 0 is a nested field group.
    struct Field {
        SEXP name;  // CHARSXP
        int group;
    };
    struct Fields {
        std::vector<std::vector<Field>> groups;
    };

    Kind kind(Val v) const;
    R_xlen_t obj_size(Val v) const;
    SEXP obj_names(Val v) const;
    Val obj_value(Val v, R_xlen_t j) const;
    R_xlen_t arr_size(Val v) const;
    Val arr_elem(Val v, R_xlen_t k) const;

    void line(size_t depth);
    void prim(Val v, bool root = false);
    void check_unique(SEXP names);

    bool all_prim(Val arr) const;
    bool build_group(const std::vector<Val>& objs, Fields& f, size_t gi);
    bool tabular(Val arr, Fields& f);
    bool keyed(Val obj, Fields& f);
    bool df_leaves(SEXP df, Fields& f, size_t gi, std::vector<PrimVec>& leaves);

    void header(R_xlen_t n, bool keyed, const Fields* f);
    void field_list(const Fields& f, size_t gi);
    void cells(Val obj, const Fields& f, size_t gi, bool& first);

    void field(SEXP key, Val v, size_t depth, bool on_hyphen);
    void fields_of(Val obj, size_t depth);
    void array_after_key(Val arr, size_t depth, bool has_key);
    void keyed_rows(Val obj, const Fields& f, size_t depth);
    void item(Val v, size_t depth);

    Out& o_;
    WriteOptions opts_;
    SEXP checked_names_ = nullptr;
};

// Writes a tabular array of data.frame batches whose row count is only known
// at the end: rows go to a temporary file, and close() writes the header and
// copies the rows after it.
class StreamWriter {
public:
    StreamWriter(const std::string& path, const std::string& tmp_path,
                 std::vector<std::string> fields, const WriteOptions& opts);
    ~StreamWriter();

    void write_batch(SEXP df);

    // commit = false discards the rows and leaves `path` untouched.
    void close(bool commit);

    size_t rows() const { return rows_; }

private:
    std::string path_;
    std::string tmp_path_;
    std::vector<std::string> fields_;
    WriteOptions opts_;
    std::FILE* tmp_ = nullptr;
    Out out_;
    size_t rows_ = 0;
    bool closed_ = false;
};

}  // namespace toonlite

#endif  // TOON_ENCODER_H
