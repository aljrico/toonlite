#ifndef TOON_DF_HPP
#define TOON_DF_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "toon_errors.h"
#include "toon_io.h"
#include "toon_lex.h"
#include "toon_r.h"

namespace toonlite {

enum class ColType { UNKNOWN, LOGICAL, INTEGER, DOUBLE, STRING };

// Builds one data.frame column from cell tokens. The type is inferred from
// the values and only ever widens: logical -> character, integer -> double
// -> character. Booleans mixed with numbers give character. Numbers and
// booleans that end up in a character column are written in canonical TOON
// form.
class ColBuilder {
public:
    explicit ColBuilder(std::string name, ColType forced = ColType::UNKNOWN);

    const std::string& name() const { return name_; }
    // Whether booleans and numbers met in this column (kept across reset()).
    bool mixed() const { return mixed_; }
    size_t size() const { return n_; }

    // Parse a trimmed cell token and append it. `scratch` holds decoded
    // quoted strings.
    void push(std::string_view token, std::string& scratch);
    void push_null();

    // Expected number of rows; storage is reserved once the type is known.
    void reserve(size_t n);

    // Drop the values but keep the allocated capacity (streaming batches).
    void reset();

    // Unprotected R vector with the column's values.
    SEXP finalize() const;

private:
    struct StrRef {
        size_t off;
        size_t len;
    };
    static constexpr size_t kNA = static_cast<size_t>(-1);

    void set_type(ColType t);
    void promote(ColType t);
    void push_string(std::string_view s);
    void push_text_of_number(const Scalar& s);
    void mix_bools_and_numbers();

    std::string name_;
    ColType forced_;
    ColType type_;
    size_t n_ = 0;
    size_t reserve_ = 0;
    bool mixed_ = false;
    std::vector<int> ints_;     // LOGICAL and INTEGER (NA_INTEGER == NA_LOGICAL)
    std::vector<double> dbls_;  // NA_REAL for missing
    std::vector<StrRef> strs_;  // off == kNA for missing
    std::string pool_;
};

// Options for read_toon_df() and toon_stream_rows().
struct TableReadOptions {
    ReadOptions read;
    bool warn = true;
    bool ragged_expand = false;    // ragged_rows = "expand_warn" (extension)
    bool n_mismatch_warn = false;  // n_mismatch = "warn" in strict mode (extension)
    size_t max_extra_cols = SIZE_MAX;
    bool has_key = false;
    std::string key;
    std::vector<std::pair<std::string, ColType>> col_types;
};

// Reads the tabular array at the document root, or at root[key], into column
// builders, in one go or in batches. Nested field groups become columns named
// by their dotted path ("customer.name").
class TabularReader {
public:
    TabularReader(const std::string& path, const TableReadOptions& opts);

    // Read up to `max_rows` more rows (0: all remaining). Returns the number
    // of rows read; 0 once the table is exhausted.
    size_t read(size_t max_rows);

    // The buffered rows as a data.frame (unprotected); the builders are reset.
    SEXP take_batch();

    // Count checks, trailing content and warnings, after the last read().
    void finish(std::vector<std::string>& warnings);

private:
    void locate();
    void setup_columns();
    void add_extra_column(size_t cell_index);

    std::string path_;
    TableReadOptions opts_;
    BufferedReader src_;
    LineReader lines_;
    Header header_;
    std::unique_ptr<RowScanner> rows_;
    bool empty_table_ = false;
    bool exhausted_ = false;

    std::vector<ColBuilder> cols_;
    std::vector<int> cell_col_;  // column for each cell index, -1 if dropped
    std::vector<std::string_view> cells_;
    std::string scratch_;
    size_t batch_rows_ = 0;
    size_t total_rows_ = 0;
    size_t min_cells_ = SIZE_MAX;
    size_t max_cells_ = 0;
    size_t extra_cols_ = 0;
};

// data.frame from column builders (unprotected).
SEXP build_dataframe(const std::vector<ColBuilder>& columns, size_t nrow);

}  // namespace toonlite

#endif  // TOON_DF_HPP
