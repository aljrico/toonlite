#ifndef TOON_STREAM_HPP
#define TOON_STREAM_HPP

#include <string>
#include <vector>
#include <functional>
#include <cstddef>
#include "toon_errors.h"
#include "toon_io.h"
#include "toon_df.h"

#include <R.h>
#include <Rinternals.h>

namespace toonlite {

// Streaming options
struct StreamOptions {
    bool strict = true;
    bool allow_comments = true;
    bool allow_duplicate_keys = true;
    bool warn = true;
    bool simplify = true;
    std::string ragged_rows = "expand_warn";
    std::string n_mismatch = "warn";
    size_t max_extra_cols = SIZE_MAX;
    std::optional<std::string> key;
    std::vector<std::pair<std::string, ColType>> col_types;
    size_t batch_size = 10000;
};

// Row streaming parser
class RowStreamer {
public:
    RowStreamer(const std::string& filepath, const StreamOptions& opts);

    // Stream rows to R callback
    void stream(SEXP callback);

    // Get accumulated warnings
    const std::vector<Warning>& warnings() const { return warnings_; }

private:
    void process_batch();
    bool find_tabular_header();
    bool parse_header(std::string_view header);
    std::vector<std::string_view> split_row(std::string_view line, char delimiter);
    std::string_view trim(std::string_view sv);

    std::string filepath_;
    StreamOptions opts_;
    std::unique_ptr<BufferedReader> reader_;
    std::vector<Warning> warnings_;

    // Schema
    std::vector<std::string> field_names_;
    size_t declared_rows_ = 0;
    size_t observed_rows_ = 0;
    char delimiter_ = ',';

    // Batch accumulation
    std::vector<ColBuilder> batch_columns_;
    size_t batch_rows_ = 0;

    // Ragged row tracking
    size_t min_fields_ = SIZE_MAX;
    size_t max_fields_ = 0;
    size_t schema_expansions_ = 0;
};

// Item streaming parser (for non-tabular arrays)
class ItemStreamer {
public:
    ItemStreamer(const std::string& filepath, const StreamOptions& opts);

    // Stream items to R callback
    void stream(SEXP callback);

    const std::vector<Warning>& warnings() const { return warnings_; }

private:
    std::string filepath_;
    StreamOptions opts_;
    std::vector<Warning> warnings_;
};

// Streaming writer for tabular data
class StreamWriter {
public:
    StreamWriter(const std::string& filepath, const std::vector<std::string>& schema, int indent = 2);
    ~StreamWriter();

    // Write a batch of rows
    void write_batch(SEXP df_batch);

    // Finalize and close
    void close();

private:
    std::string filepath_;
    std::vector<std::string> schema_;
    int indent_;
    std::ofstream file_;
    size_t rows_written_ = 0;
    bool header_written_ = false;
    bool closed_ = false;

    void write_header();
    void write_row(SEXP df, R_xlen_t row);
};

} // namespace toonlite

#endif // TOON_STREAM_HPP
