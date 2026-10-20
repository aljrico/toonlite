#include "toon_lex.h"
#include "toon_charconv.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace toonlite {

namespace {

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Cut `s` to at most `max` bytes without splitting a UTF-8 sequence.
std::string snippet_of(std::string_view s, size_t max = 120) {
    if (s.size() <= max) return std::string(s);
    size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) cut--;
    return std::string(s.substr(0, cut)) + "...";
}

// One pass over `s`: split on unquoted `delim` and trim each token. With
// `row_check`, return false as soon as an unquoted colon appears before the
// first unquoted delimiter (the line is not a tabular row, spec 9.3).
bool split_impl(std::string_view s, char delim, std::vector<std::string_view>& out,
                bool row_check) {
    out.clear();
    const char* p = s.data();
    const size_t n = s.size();
    size_t start = 0;
    bool inq = false;
    bool seen_delim = false;
    for (size_t i = 0; i < n; i++) {
        const char c = p[i];
        if (inq) {
            if (c == '\\') {
                i++;
            } else if (c == '"') {
                inq = false;
            }
            continue;
        }
        if (c == '"') {
            inq = true;
        } else if (c == delim) {
            out.push_back(trim_spaces(std::string_view(p + start, i - start)));
            start = i + 1;
            seen_delim = true;
        } else if (row_check && c == ':' && !seen_delim) {
            return false;
        }
    }
    out.push_back(trim_spaces(std::string_view(p + start, n - start)));
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// LineReader
// ---------------------------------------------------------------------------

LineReader::LineReader(BufferedReader& src, const ReadOptions& opts, std::string file)
    : src_(src), opts_(opts), file_(std::move(file)) {
    if (opts_.indent < 1) opts_.indent = 1;
}

void LineReader::fail(const std::string& msg, const Line* at) const {
    if (at) {
        fail(msg, at->no, at->content);
    }
    fail(msg, size_t{0});
}

void LineReader::fail(const std::string& msg, size_t line_no, std::string_view snippet) const {
    throw ParseError(msg, line_no, 0, snippet_of(snippet), file_);
}

void LineReader::fetch() {
    std::string_view raw;
    size_t no = 0;
    const size_t indent = static_cast<size_t>(opts_.indent);
    while (true) {
        if (!src_.next_line(raw, no)) {
            eof_ = true;
            return;
        }
        if (first_) {
            first_ = false;
            if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
                static_cast<unsigned char>(raw[1]) == 0xBB &&
                static_cast<unsigned char>(raw[2]) == 0xBF) {
                raw.remove_prefix(3);
            }
        }
        if (!valid_utf8(raw.data(), raw.size())) {
            fail("Invalid UTF-8 in input", no);
        }
        if (std::memchr(raw.data(), '\0', raw.size()) != nullptr) {
            fail("NUL characters cannot be represented in R strings", no);
        }

        size_t e = raw.size();
        while (e > 0 && raw[e - 1] == ' ') e--;
        raw = raw.substr(0, e);
        if (raw.empty()) {
            pending_blank_ = true;
            continue;
        }

        // Comment line: '#' after zero or more spaces (spec 5.1).
        size_t i = 0;
        while (i < raw.size() && raw[i] == ' ') i++;
        if (i < raw.size() && raw[i] == '#') continue;

        size_t spaces = 0, tabs = 0, j = 0;
        while (j < raw.size() && (raw[j] == ' ' || raw[j] == '\t')) {
            if (raw[j] == '\t') {
                tabs++;
            } else {
                spaces++;
            }
            j++;
        }
        if (j == raw.size()) {
            // Only spaces and tabs (at least one tab, as trailing spaces are
            // gone): blank in non-strict mode (spec 14.4 recovery 3).
            if (opts_.strict) {
                fail("Tab characters are not allowed in indentation", no, raw);
            }
            pending_blank_ = true;
            continue;
        }

        size_t depth;
        if (tabs > 0) {
            if (opts_.strict) {
                fail("Tab characters are not allowed in indentation", no, raw);
            }
            depth = tabs + spaces / indent;
        } else {
            if (opts_.strict && spaces % indent != 0) {
                fail("Indentation of " + std::to_string(spaces) +
                         " spaces is not a multiple of the indent size (" +
                         std::to_string(indent) + ")",
                     no, raw);
            }
            depth = spaces / indent;
        }

        cur_.content = raw.substr(j);
        cur_.depth = depth;
        cur_.no = no;
        cur_.blank_before = pending_blank_;
        pending_blank_ = false;
        has_cur_ = true;
        return;
    }
}

void LineReader::consume() {
    if (cur_.blank_before && spans_ > 0 && opts_.strict) {
        fail("Blank lines are not allowed inside an array or keyed object", &cur_);
    }
    has_cur_ = false;
}

// ---------------------------------------------------------------------------
// UTF-8, scanning, splitting
// ---------------------------------------------------------------------------

bool valid_utf8(const char* s, size_t n) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    const unsigned char* end = p + n;
    while (p < end) {
        if (end - p >= 8) {
            uint64_t w;
            std::memcpy(&w, p, 8);
            if ((w & 0x8080808080808080ULL) == 0) {
                p += 8;
                continue;
            }
        }
        const unsigned char c = *p;
        if (c < 0x80) {
            p++;
        } else if (c < 0xC2) {
            return false;
        } else if (c < 0xE0) {
            if (end - p < 2 || (p[1] & 0xC0) != 0x80) return false;
            p += 2;
        } else if (c < 0xF0) {
            if (end - p < 3) return false;
            const unsigned char c1 = p[1];
            if ((c1 & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;
            if (c == 0xE0 && c1 < 0xA0) return false;  // overlong
            if (c == 0xED && c1 > 0x9F) return false;  // surrogate
            p += 3;
        } else if (c < 0xF5) {
            if (end - p < 4) return false;
            const unsigned char c1 = p[1];
            if ((c1 & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) {
                return false;
            }
            if (c == 0xF0 && c1 < 0x90) return false;  // overlong
            if (c == 0xF4 && c1 > 0x8F) return false;  // above U+10FFFF
            p += 4;
        } else {
            return false;
        }
    }
    return true;
}

LineScan scan_line(std::string_view s, char delim) {
    LineScan r;
    bool inq = false;
    const size_t n = s.size();
    for (size_t i = 0; i < n; i++) {
        const char c = s[i];
        if (inq) {
            if (c == '\\') {
                i++;
            } else if (c == '"') {
                inq = false;
            }
            continue;
        }
        if (c == '"') {
            inq = true;
        } else if (c == ':') {
            if (r.colon == npos) r.colon = i;
        } else if (c == '[') {
            if (r.bracket == npos) r.bracket = i;
        } else if (delim != 0 && c == delim) {
            if (r.delim == npos) r.delim = i;
        }
    }
    return r;
}

void split_delimited(std::string_view s, char delim, std::vector<std::string_view>& out) {
    split_impl(s, delim, out, false);
}

// ---------------------------------------------------------------------------
// Scalars
// ---------------------------------------------------------------------------

void unescape_quoted(std::string_view tok, std::string& out) {
    out.clear();
    const size_t n = tok.size();
    size_t i = 1;
    while (i < n) {
        const char c = tok[i];
        if (c == '"') {
            if (i != n - 1) {
                throw LexError("Unexpected characters after a closing quote");
            }
            return;
        }
        if (c != '\\') {
            size_t j = i + 1;
            while (j < n && tok[j] != '"' && tok[j] != '\\') j++;
            out.append(tok.data() + i, j - i);
            i = j;
            continue;
        }
        if (i + 1 >= n) break;
        const char e = tok[i + 1];
        switch (e) {
            case '\\': out += '\\'; break;
            case '"': out += '"'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                for (size_t k = 0; k < 4; k++) {
                    const int h = (i + 2 + k < n) ? hex_value(tok[i + 2 + k]) : -1;
                    if (h < 0) {
                        throw LexError("Invalid \\u escape: expected four hex digits");
                    }
                    cp = cp * 16 + static_cast<unsigned>(h);
                }
                if (cp >= 0xD800 && cp <= 0xDFFF) {
                    throw LexError("Invalid \\u escape: surrogate code points are not allowed");
                }
                if (cp == 0) {
                    throw LexError("\\u0000 cannot be represented in R strings");
                }
                append_utf8(out, cp);
                i += 6;
                continue;
            }
            default:
                throw LexError(std::string("Invalid escape sequence \\") + e);
        }
        i += 2;
    }
    throw LexError("Unterminated string");
}

void decode_key(std::string_view tok, std::string& out) {
    if (!tok.empty() && tok[0] == '"') {
        unescape_quoted(tok, out);
    } else {
        out.assign(tok.data(), tok.size());
    }
}

bool is_number_token(std::string_view s, bool& integer) {
    const size_t n = s.size();
    size_t i = 0;
    if (i < n && s[i] == '-') i++;
    if (i >= n) return false;
    if (s[i] == '0') {
        i++;
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < n && is_digit(s[i])) i++;
    } else {
        return false;
    }
    integer = true;
    if (i < n && s[i] == '.') {
        i++;
        if (i >= n || !is_digit(s[i])) return false;
        while (i < n && is_digit(s[i])) i++;
        integer = false;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        if (i >= n || !is_digit(s[i])) return false;
        while (i < n && is_digit(s[i])) i++;
        integer = false;
    }
    return i == n;
}

Scalar parse_scalar(std::string_view tok, std::string& scratch) {
    Scalar s;
    if (!tok.empty() && tok[0] == '"') {
        unescape_quoted(tok, scratch);
        s.kind = ScalarKind::String;
        s.quoted = true;
        s.str = scratch;
        return s;
    }
    switch (tok.size()) {
        case 4:
            if (tok == "null") {
                s.kind = ScalarKind::Null;
                return s;
            }
            if (tok == "true") {
                s.kind = ScalarKind::Bool;
                s.b = true;
                return s;
            }
            break;
        case 5:
            if (tok == "false") {
                s.kind = ScalarKind::Bool;
                s.b = false;
                return s;
            }
            break;
        default:
            break;
    }
    bool integer = false;
    if (!tok.empty() && (is_digit(tok[0]) || tok[0] == '-') && is_number_token(tok, integer)) {
        if (integer) {
            const bool neg = tok[0] == '-';
            const size_t ndig = tok.size() - (neg ? 1 : 0);
            if (ndig <= 10) {
                int64_t v = 0;
                for (size_t k = neg ? 1 : 0; k < tok.size(); k++) v = v * 10 + (tok[k] - '0');
                if (neg) v = -v;
                // INT32_MIN is R's NA_integer_: read it as a double instead.
                if (v > INT32_MIN && v <= INT32_MAX) {
                    s.kind = ScalarKind::Int;
                    s.i = static_cast<int32_t>(v);
                    return s;
                }
            }
        }
        double d = 0.0;
        auto res = double_from_chars(tok.data(), tok.data() + tok.size(), d);
        if (res.ec == std::errc::result_out_of_range) {
            // Beyond the double range: nearest double (+-Inf or 0).
            std::string tmp(tok);
            errno = 0;
            d = std::strtod(tmp.c_str(), nullptr);
        }
        s.kind = ScalarKind::Double;
        s.d = d;
        return s;
    }
    s.kind = ScalarKind::String;
    s.str = tok;
    return s;
}

// ---------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------

namespace {

size_t parse_field_list(std::string_view s, size_t p, char delim, bool strict,
                        FieldList& fl, size_t level = 1) {
    // s[p] == '{'
    if (level > kMaxNesting) {
        throw LexError("Field groups are nested deeper than " + std::to_string(kMaxNesting) +
                       " levels");
    }
    const size_t g = fl.groups.size();
    fl.groups.emplace_back();
    const size_t n = s.size();
    p++;
    while (true) {
        while (p < n && s[p] == ' ') p++;
        const size_t name_start = p;
        bool inq = false;
        while (p < n) {
            const char c = s[p];
            if (inq) {
                if (c == '\\') {
                    p += 2;
                    continue;
                }
                if (c == '"') inq = false;
                p++;
                continue;
            }
            if (c == '"') {
                inq = true;
                p++;
                continue;
            }
            if (c == delim || c == '{' || c == '}') break;
            if (c == ',' || c == '|' || c == '\t') {
                throw LexError("Field list uses a delimiter other than the one declared in its bracket segment");
            }
            p++;
        }
        if (p >= n) {
            throw LexError("Unmatched '{' in field list");
        }
        std::string_view tok = s.substr(name_start, p - name_start);
        FieldEntry e;
        if (s[p] == '{') {
            if (tok.empty()) {
                throw LexError("Nested field group without a field name");
            }
            if (tok.back() == ' ' || tok.back() == '\t') {
                throw LexError("Whitespace is not allowed between a field name and its nested field group");
            }
            decode_key(tok, e.name);
            e.group = static_cast<int>(fl.groups.size());
            fl.groups[g].push_back(std::move(e));
            p = parse_field_list(s, p, delim, strict, fl, level + 1);
            while (p < n && s[p] == ' ') p++;
            if (p >= n) {
                throw LexError("Unmatched '{' in field list");
            }
            if (s[p] != delim && s[p] != '}') {
                throw LexError("Unexpected content after a nested field group");
            }
        } else {
            tok = trim_spaces(tok);
            if (tok.empty()) {
                throw LexError(s[p] == '}' && fl.groups[g].empty() ? "Empty field list"
                                                                   : "Empty field name in field list");
            }
            decode_key(tok, e.name);
            e.leaf = static_cast<int>(fl.leaf_count++);
            fl.groups[g].push_back(std::move(e));
        }
        if (s[p] == delim) {
            p++;
            continue;
        }
        p++;  // '}'
        break;
    }
    if (strict) {
        const auto& entries = fl.groups[g];
        for (size_t a = 1; a < entries.size(); a++) {
            for (size_t b = 0; b < a; b++) {
                if (entries[a].name == entries[b].name) {
                    throw LexError("Duplicate field name '" + entries[a].name + "' in field list");
                }
            }
        }
    }
    return p;
}

int resolve_group(const FieldList& fl, size_t g, std::vector<ResolvedGroup>& out) {
    const int idx = static_cast<int>(out.size());
    out.emplace_back();
    for (const auto& e : fl.groups[g]) {
        const int sub = e.group >= 0 ? resolve_group(fl, static_cast<size_t>(e.group), out) : -1;
        ResolvedGroup& rg = out[static_cast<size_t>(idx)];
        size_t j = 0;
        while (j < rg.keys.size() && rg.keys[j] != e.name) j++;
        if (j == rg.keys.size()) {
            rg.keys.push_back(e.name);
            rg.leaf.push_back(e.leaf);
            rg.group.push_back(sub);
        } else {
            rg.leaf[j] = e.leaf;
            rg.group[j] = sub;
        }
    }
    return idx;
}

}  // namespace

std::vector<ResolvedGroup> resolve_fields(const FieldList& fl) {
    std::vector<ResolvedGroup> out;
    if (!fl.groups.empty()) resolve_group(fl, 0, out);
    return out;
}

void parse_header(std::string_view s, const LineScan& sc, bool strict, Header& h) {
    h.has_key = false;
    h.key.clear();
    h.length = 0;
    h.length_overflow = false;
    h.keyed = false;
    h.delim = ',';
    h.has_fields = false;
    h.fields.groups.clear();
    h.fields.leaf_count = 0;
    h.inline_values = std::string_view();

    const size_t n = s.size();
    const size_t lb = sc.bracket;
    if (lb > 0) {
        std::string_view key = s.substr(0, lb);
        if (key.back() == ' ' || key.back() == '\t') {
            throw LexError("Whitespace is not allowed between a key and its bracket segment");
        }
        h.has_key = true;
        decode_key(key, h.key);
    }

    size_t p = lb + 1;
    if (p >= n || !is_digit(s[p])) {
        throw LexError("Invalid array length in header");
    }
    if (s[p] == '0' && p + 1 < n && is_digit(s[p + 1])) {
        throw LexError("Array length must not have leading zeros");
    }
    while (p < n && is_digit(s[p])) {
        const uint64_t d = static_cast<uint64_t>(s[p] - '0');
        if (!h.length_overflow) {
            if (h.length > (UINT64_MAX - d) / 10) {
                h.length_overflow = true;
            } else {
                h.length = h.length * 10 + d;
            }
        }
        p++;
    }
    if (p < n && s[p] == ':') {
        h.keyed = true;
        p++;
    }
    if (p < n && (s[p] == '\t' || s[p] == '|')) {
        h.delim = s[p];
        p++;
    }
    if (p >= n || s[p] != ']') {
        throw LexError("Malformed bracket segment in header");
    }
    p++;
    if (p < n && s[p] == '{') {
        h.has_fields = true;
        p = parse_field_list(s, p, h.delim, strict, h.fields);
    }
    if (p >= n || s[p] != ':') {
        throw LexError(h.has_fields ? "Expected ':' right after the field list"
                                    : "Expected ':' right after the bracket segment");
    }
    p++;
    std::string_view rest = s.substr(p);
    if (h.has_fields) {
        for (char c : rest) {
            if (c != ' ') {
                throw LexError("Unexpected content after the colon of a header with a field list");
            }
        }
    } else {
        if (h.keyed) {
            throw LexError("A keyed header requires a field list");
        }
        h.inline_values = trim_spaces(rest);
    }
}

std::string counted(size_t n, std::string_view noun) {
    std::string out = std::to_string(n) + " ";
    if (n == 1) {
        out += noun;
    } else if (!noun.empty() && noun.back() == 'y') {
        out += noun.substr(0, noun.size() - 1);
        out += "ies";
    } else {
        out += noun;
        out += 's';
    }
    return out;
}

std::string count_mismatch_message(const Header& h, size_t observed, const char* noun) {
    if (h.length_overflow) {
        return "Declared length is beyond the supported range; found " + counted(observed, noun);
    }
    return "Declared " + counted(static_cast<size_t>(h.length), noun) + " but found " +
           std::to_string(observed);
}

// ---------------------------------------------------------------------------
// RowScanner
// ---------------------------------------------------------------------------

RowScanner::RowScanner(LineReader& r, const Header& h, size_t depth, bool check_width)
    : r_(r), h_(h), depth_(depth), check_width_(check_width), line_no_(h.line_no) {}

RowScanner::~RowScanner() {
    if (!done_ && started_) r_.span_ended();
}

void RowScanner::end() {
    if (started_ && !done_) r_.span_ended();
    done_ = true;
}

bool RowScanner::next(std::vector<std::string_view>& cells, std::string_view* key) {
    if (done_) return false;
    const Line* L = r_.peek();
    if (!L) {
        end();
        return false;
    }
    if (!entered_) {
        if (L->depth < depth_) {
            end();
            return false;
        }
        if (L->depth > depth_ && r_.strict()) {
            r_.fail("Indentation jumps more than one level", L);
        }
        actual_ = L->depth;
        entered_ = true;
    }
    if (L->depth < actual_) {
        if (L->depth >= depth_) {
            r_.fail(h_.keyed ? "Line is shallower than the entry rows above it"
                             : "Line is shallower than the rows above it",
                    L);
        }
        end();
        return false;
    }
    if (L->depth > actual_) {
        r_.fail(h_.keyed ? "Unexpected indentation after an entry row"
                         : "Unexpected indentation after a tabular row",
                L);
    }
    if (h_.keyed) {
        // Split at the first unquoted colon, then on the delimiter (9.5).
        const size_t colon = scan_line(L->content).colon;
        if (colon == npos) {
            r_.fail("Expected an entry row ('key: values') in a keyed table", L);
        }
        *key = trim_spaces(L->content.substr(0, colon));
        const std::string_view rest = trim_spaces(L->content.substr(colon + 1));
        if (rest.empty()) {
            cells.clear();
        } else {
            split_impl(rest, h_.delim, cells, false);
        }
    } else if (!split_impl(L->content, h_.delim, cells, true)) {
        r_.fail("Expected a tabular row, found a key-value line", L);
    }
    if (check_width_ && cells.size() != h_.fields.leaf_count) {
        r_.fail((h_.keyed ? "Entry row has " : "Tabular row has ") + counted(cells.size(), "value") +
                    " but the header declares " + counted(h_.fields.leaf_count, "field"),
                L);
    }
    line_no_ = L->no;
    line_ = L->content;
    r_.consume();
    if (!started_) {
        r_.span_started();
        started_ = true;
    }
    rows_++;
    return true;
}

}  // namespace toonlite
