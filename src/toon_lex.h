#ifndef TOON_LEX_H
#define TOON_LEX_H

// Shared lexical layer for every TOON reader (DOM parser, data frame reader,
// row and item streamers). Everything that decides how a line or a token is
// read lives here so the readers cannot drift apart:
//
//   * line preparation (spec section 12): byte-order mark, CR, trailing
//     spaces, comment lines (5.1), blank lines, indentation and depth,
//     UTF-8 validation, blank lines inside header spans
//   * quote-aware scanning and splitting (5.2, 11.2)
//   * scalar tokens: quoted-string unescaping (7.1) and type inference (4)
//   * array headers with keyed markers, delimiters and nested field
//     groups (6, 9.3, 9.5)
//   * the tabular row loop (9.3)

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "toon_errors.h"
#include "toon_io.h"

namespace toonlite {

constexpr size_t npos = std::string_view::npos;

// Decoder options shared by every reader (spec section 13).
struct ReadOptions {
    bool strict = true;
    int indent = 2;  // indentSize
};

// A lexical error without location. Readers turn it into a ParseError that
// names the line (LineReader::fail).
class LexError : public std::runtime_error {
public:
    explicit LexError(const std::string& msg) : std::runtime_error(msg) {}
};

// Trim U+0020 only (spec 1.2, 12): tabs and other spaces are content.
inline std::string_view trim_spaces(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && s[b] == ' ') b++;
    while (e > b && s[e - 1] == ' ') e--;
    return s.substr(b, e - b);
}

// ---------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------

struct Line {
    std::string_view content;  // after indentation; trailing spaces removed
    size_t depth = 0;
    size_t no = 0;             // 1-based physical line number
    bool blank_before = false; // a blank line precedes it (comments ignored)
};

// Yields prepared, non-blank, non-comment lines with one line of lookahead.
// A peeked line's content stays valid until the next peek().
class LineReader {
public:
    LineReader(BufferedReader& src, const ReadOptions& opts, std::string file = "");

    // Next content line, or nullptr at end of input.
    const Line* peek() {
        if (!has_cur_ && !eof_) fetch();
        return has_cur_ ? &cur_ : nullptr;
    }

    // Accept the peeked line as part of the document. A blank line before it
    // inside an open header span is an error in strict mode (spec 12).
    void consume();

    // Header spans (spec 12). A scope calls span_started() right after
    // consuming its first item, row or entry line, and span_ended() when it
    // closes, if it started.
    void span_started() { spans_++; }
    void span_ended() { spans_--; }

    const ReadOptions& options() const { return opts_; }
    bool strict() const { return opts_.strict; }
    const std::string& file() const { return file_; }

    [[noreturn]] void fail(const std::string& msg, const Line* at) const;
    [[noreturn]] void fail(const std::string& msg, size_t line_no,
                           std::string_view snippet = {}) const;

private:
    void fetch();

    BufferedReader& src_;
    ReadOptions opts_;
    std::string file_;
    Line cur_;
    bool has_cur_ = false;
    bool eof_ = false;
    bool first_ = true;
    bool pending_blank_ = false;
    int spans_ = 0;
};

// Validate UTF-8: rejects overlong forms, surrogates and code points above
// U+10FFFF (spec 4).
bool valid_utf8(const char* p, size_t n);

// ---------------------------------------------------------------------------
// Quote-aware scanning
// ---------------------------------------------------------------------------

// A '"' opens a quoted span anywhere in a line; inside it a backslash escapes
// the next character, and the next unescaped '"' closes it. Without a closing
// quote the span runs to the end of the line (spec 5.2).
struct LineScan {
    size_t colon = npos;    // first unquoted ':'
    size_t bracket = npos;  // first unquoted '['
    size_t delim = npos;    // first unquoted active delimiter
};

// `delim` == 0 skips the delimiter search.
LineScan scan_line(std::string_view s, char delim = 0);

// An array-header line: an unquoted colon with an unquoted '[' before it.
inline bool is_header_line(const LineScan& sc) {
    return sc.colon != npos && sc.bracket != npos && sc.bracket < sc.colon;
}

// A list-item line: "-" alone or "-" followed by spaces (spec 5.2). Returns
// the remainder after the marker and its spaces.
inline bool list_item_rest(std::string_view content, std::string_view& rest) {
    if (content.empty() || content[0] != '-') return false;
    if (content.size() == 1) {
        rest = std::string_view();
        return true;
    }
    if (content[1] != ' ') return false;
    size_t i = 1;
    while (i < content.size() && content[i] == ' ') i++;
    rest = content.substr(i);
    return true;
}

// Split on unquoted `delim`, trimming spaces around every token. Empty tokens
// are kept (spec 11.2). `out` is cleared first so callers can reuse it.
void split_delimited(std::string_view s, char delim,
                     std::vector<std::string_view>& out);

// ---------------------------------------------------------------------------
// Scalars
// ---------------------------------------------------------------------------

// Decode a quoted token (spec 7.1). The token must start with '"' and end at
// its closing quote; escapes other than \\ \" \n \r \t \uXXXX, surrogate
// escapes, unterminated strings and text after the closing quote throw.
void unescape_quoted(std::string_view token, std::string& out);

// Decode a key or field-name token (spec 7.4): quoted tokens are unescaped,
// anything else is literal.
void decode_key(std::string_view token, std::string& out);

// True when `tok` matches the JSON number grammar (spec 4). `integer` is set
// when it has neither fraction nor exponent.
bool is_number_token(std::string_view tok, bool& integer);

enum class ScalarKind : uint8_t { Null, Bool, Int, Double, String };

struct Scalar {
    ScalarKind kind = ScalarKind::Null;
    bool quoted = false;
    bool b = false;
    int32_t i = 0;
    double d = 0.0;
    std::string_view str;  // String: token text or decoded text in scratch
};

// Type a trimmed value token (spec 4). Quoted tokens decode into `scratch`
// and `str` points at it. Integers that fit R's integer type (excluding
// -2147483648, which is NA_integer_) become Int, other numbers Double
// (nearest double; magnitudes beyond the double range become +-Inf).
Scalar parse_scalar(std::string_view tok, std::string& scratch);

// ---------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------

struct FieldEntry {
    std::string name;
    int leaf = -1;   // leaf index (depth-first pre-order), or -1 for a group
    int group = -1;  // index into FieldList::groups for a nested field group
};

// A header's field list as written, duplicates included.
struct FieldList {
    std::vector<std::vector<FieldEntry>> groups;  // groups[0]: top level
    size_t leaf_count = 0;
};

// The field list after resolving duplicate names last-write-wins (spec
// 14.4): every key appears once, at the position of its first occurrence.
struct ResolvedGroup {
    std::vector<std::string> keys;
    std::vector<int> leaf;   // per key: leaf index, or -1
    std::vector<int> group;  // per key: resolved group index, or -1
};
std::vector<ResolvedGroup> resolve_fields(const FieldList& fl);

struct Header {
    bool has_key = false;
    std::string key;
    uint64_t length = 0;
    bool length_overflow = false;  // declared length beyond uint64
    bool keyed = false;            // [N:] keyed tabular header (spec 9.5)
    char delim = ',';
    bool has_fields = false;
    FieldList fields;
    std::string_view inline_values;  // text after the colon (no field list)
    size_t line_no = 0;

    bool count_matches(uint64_t n) const {
        return !length_overflow && n == length;
    }
};

// Parse an array-header line (spec 6). `sc` is scan_line(content). Throws
// LexError on any header syntax error. Duplicate field names are an error
// when `strict`.
void parse_header(std::string_view content, const LineScan& sc, bool strict,
                  Header& h);

// ---------------------------------------------------------------------------
// Tabular rows
// ---------------------------------------------------------------------------

// Iterates the rows of a tabular array whose rows belong at `depth` (spec
// 9.3): depth jumps (non-strict recovery 14.4), over-indented lines, lines at
// row depth that are not rows, row widths and header spans.
class RowScanner {
public:
    RowScanner(LineReader& r, const Header& h, size_t depth,
               bool check_width = true);
    ~RowScanner();

    // Cells of the next row (views into the line, valid until the next call),
    // or false once the scope has ended.
    bool next(std::vector<std::string_view>& cells);

    size_t rows() const { return rows_; }
    size_t line_no() const { return line_no_; }

private:
    void end();

    LineReader& r_;
    const Header& h_;
    size_t depth_;
    size_t actual_ = 0;
    bool entered_ = false;
    bool started_ = false;
    bool done_ = false;
    bool check_width_;
    size_t rows_ = 0;
    size_t line_no_ = 0;
};

// Shared message for declared-count mismatches.
std::string count_mismatch_message(const Header& h, size_t observed,
                                   const char* what);

}  // namespace toonlite

#endif  // TOON_LEX_H
