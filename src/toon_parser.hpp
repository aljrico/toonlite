#ifndef TOON_PARSER_HPP
#define TOON_PARSER_HPP

#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <memory>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include "toon_errors.hpp"
#include "toon_io.hpp"

namespace toonlite {

// Forward declarations
struct Node;
using NodePtr = std::shared_ptr<Node>;

// Node kinds
enum class NodeKind {
    N_NULL,
    N_BOOL,
    N_INT,
    N_DOUBLE,
    N_STRING,
    N_ARRAY,
    N_OBJECT
};

// DOM node representing parsed TOON value
struct Node {
    NodeKind kind;

    // Value storage
    bool bool_val = false;
    int64_t int_val = 0;
    double double_val = 0.0;
    std::string string_val;

    // Children for array/object
    std::vector<NodePtr> array_items;
    std::vector<std::pair<std::string, NodePtr>> object_items;

    // Factory methods
    static NodePtr make_null();
    static NodePtr make_bool(bool v);
    static NodePtr make_int(int64_t v);
    static NodePtr make_double(double v);
    static NodePtr make_string(const std::string& v);
    static NodePtr make_string(std::string_view v);
    static NodePtr make_array();
    static NodePtr make_object();
};

// Parser options
struct ParseOptions {
    bool strict = true;
    bool simplify = true;
    bool allow_comments = true;
    bool allow_duplicate_keys = true;
    bool warn = true;
};

// Tabular array header info
struct TabularHeader {
    size_t declared_count = 0;
    std::vector<std::string> fields;
    char delimiter = ',';
    bool is_tabular = false;
};

// Line classification
enum class LineType {
    EMPTY,
    COMMENT,
    LIST_ITEM,      // - value
    KEY_VALUE,      // key: value (inline)
    KEY_NESTED,     // key: (followed by nested block)
    ARRAY_HEADER,   // [N]:
    TABULAR_HEADER, // [N]{fields}:
    RAW_VALUE       // primitive value
};

struct LineInfo {
    LineType type;
    size_t indent;
    std::string_view content;  // After indentation
    std::string_view key;      // For KEY_* types
    std::string_view value;    // For KEY_VALUE, LIST_ITEM, RAW_VALUE
    TabularHeader tabular;     // For ARRAY_HEADER, TABULAR_HEADER
    size_t line_no;
};

// Parser class
class Parser {
public:
    Parser(const ParseOptions& opts = ParseOptions());

    // Parse from string
    NodePtr parse_string(const std::string& text);
    NodePtr parse_string(const char* data, size_t len);

    // Parse from file
    NodePtr parse_file(const std::string& filepath);

    // Validate without building DOM
    ValidationResult validate_string(const std::string& text);
    ValidationResult validate_file(const std::string& filepath);

    // Get warnings accumulated during parsing
    const std::vector<Warning>& warnings() const { return warnings_; }

private:
    // Line classification
    LineInfo classify_line(std::string_view line, size_t line_no);

    // Primitive parsing
    NodePtr parse_primitive(std::string_view text);
    bool parse_null(std::string_view text);
    std::optional<bool> parse_bool(std::string_view text);
    std::optional<int64_t> parse_integer(std::string_view text);
    std::optional<double> parse_number(std::string_view text);
    std::optional<std::string> parse_quoted_string(std::string_view text);

    // Header parsing
    TabularHeader parse_array_header(std::string_view text);

    // Row parsing for tabular arrays
    std::vector<std::string_view> parse_tabular_row(std::string_view line, char delimiter);

    // Utility functions
    size_t count_indent(std::string_view line);
    std::string_view trim(std::string_view sv);
    std::string_view trim_trailing(std::string_view sv);
    bool is_comment_line(std::string_view content);
    void strip_trailing_comment(std::string_view& content);

    // Main parsing logic
    NodePtr parse_value(BufferedReader& reader, int parent_indent);
    NodePtr parse_object(BufferedReader& reader, int parent_indent, std::string_view first_key);
    NodePtr parse_array(BufferedReader& reader, int parent_indent, const TabularHeader& header);

    // Error handling
    void error(const std::string& msg, size_t line, size_t col = 0);
    std::string get_snippet(std::string_view line, size_t col);

    ParseOptions opts_;
    std::vector<Warning> warnings_;
    std::string current_file_;

    // For peeking at next line
    bool has_peeked_ = false;
    LineInfo peeked_line_;
    std::string_view peeked_raw_;
};

} // namespace toonlite

#endif // TOON_PARSER_HPP
