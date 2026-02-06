#ifndef TOON_DF_HPP
#define TOON_DF_HPP

#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <optional>
#include <cstdint>
#include "toon_errors.h"
#include "toon_io.h"

#include <R.h>
#include <Rinternals.h>
#ifdef error
#undef error
#endif
#ifdef length
#undef length
#endif
#ifdef Realloc
#undef Realloc
#endif
#ifdef Free
#undef Free
#endif

namespace toonlite {

// Column type for tabular data
enum class ColType {
    UNKNOWN,
    LOGICAL,
    INTEGER,
    DOUBLE,
    STRING
};

// Column builder for efficient vector construction
class ColBuilder {
public:
    ColBuilder(const std::string& name, size_t initial_capacity = 1000);

    const std::string& name() const { return name_; }
    ColType type() const { return type_; }
    size_t size() const { return size_; }

    // Set value at row index, handling type promotion
    void set(size_t row, std::string_view value);
    void set_null(size_t row);

    // Ensure capacity for n rows
    void ensure_capacity(size_t n);

    // Finalize and create R vector
    SEXP finalize();

    // Force specific type
    void force_type(ColType t);

private:
    void promote_to(ColType new_type);
    void parse_and_store(size_t row, std::string_view value);

    std::string name_;
    ColType type_ = ColType::UNKNOWN;
    size_t size_ = 0;
    size_t capacity_ = 0;

    // Storage buffers (only one is active based on type)
    std::vector<int> lgl_data_;      // 0/1/NA_LOGICAL
    std::vector<int> int_data_;      // NA_INTEGER for NA
    std::vector<double> dbl_data_;   // NA_REAL for NA
    std::vector<std::string> str_data_;
    std::vector<bool> na_mask_;      // Track NAs for all types
};

// Options for tabular parsing
struct TabularParseOptions {
    bool strict = true;
    bool allow_comments = true;
    bool allow_duplicate_keys = true;
    bool warn = true;
    std::string ragged_rows = "expand_warn";  // "expand_warn" or "error"
    std::string n_mismatch = "warn";          // "warn" or "error"
    size_t max_extra_cols = SIZE_MAX;
    std::optional<std::string> key;           // Extract from root[key]
    std::vector<std::pair<std::string, ColType>> col_types;  // User-specified types
};

// Tabular array parser
class TabularParser {
public:
    TabularParser(const TabularParseOptions& opts = TabularParseOptions());

    // Parse tabular TOON from file to data.frame
    SEXP parse_file(const std::string& filepath);

    // Parse tabular TOON from string to data.frame
    SEXP parse_string(const char* data, size_t len);

    // Get warnings accumulated during parsing
    const std::vector<Warning>& warnings() const { return warnings_; }

private:
    // Find tabular array in input (handles key extraction)
    bool find_tabular_array(BufferedReader& reader, std::string& header_line, size_t& header_line_no);

    // Parse tabular header [N]{field1,field2,...}:
    bool parse_header(std::string_view header);

    // Parse data rows
    void parse_rows(BufferedReader& reader, int base_indent);

    // Parse a single row line
    void parse_row_line(std::string_view line, size_t line_no);

    // Utility
    std::vector<std::string_view> split_row(std::string_view line, char delimiter);
    std::string_view trim(std::string_view sv);

    TabularParseOptions opts_;
    std::vector<Warning> warnings_;
    std::string current_file_;

    // Schema
    std::vector<std::string> field_names_;
    std::vector<ColBuilder> columns_;
    size_t declared_rows_ = 0;
    size_t observed_rows_ = 0;
    char delimiter_ = ',';

    // Ragged row tracking
    size_t min_fields_ = SIZE_MAX;
    size_t max_fields_ = 0;
    size_t schema_expansions_ = 0;
};

// Build data.frame from column builders
SEXP build_dataframe(std::vector<ColBuilder>& columns, size_t nrow);

} // namespace toonlite

#endif // TOON_DF_HPP
