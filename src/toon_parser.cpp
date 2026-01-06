#include "toon_parser.h"
#include <charconv>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace toonlite {

// Node factory methods
NodePtr Node::make_null() {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::N_NULL;
    return n;
}

NodePtr Node::make_bool(bool v) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::N_BOOL;
    n->bool_val = v;
    return n;
}

NodePtr Node::make_int(int64_t v) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::N_INT;
    n->int_val = v;
    return n;
}

NodePtr Node::make_double(double v) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::N_DOUBLE;
    n->double_val = v;
    return n;
}

NodePtr Node::make_string(const std::string& v) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::N_STRING;
    n->string_val = v;
    return n;
}

NodePtr Node::make_string(std::string_view v) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::N_STRING;
    n->string_val = std::string(v);
    return n;
}

NodePtr Node::make_array() {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::N_ARRAY;
    return n;
}

NodePtr Node::make_object() {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::N_OBJECT;
    return n;
}

// Parser implementation
Parser::Parser(const ParseOptions& opts) : opts_(opts) {}

size_t Parser::count_indent(std::string_view line) {
    size_t indent = 0;
    for (char c : line) {
        if (c == ' ') {
            indent++;
        } else if (c == '\t') {
            if (opts_.strict) {
                error("Tab characters not allowed in indentation (strict mode)", 0);
            }
            indent++; // Count tab as 1 space for non-strict mode
        } else {
            break;
        }
    }
    return indent;
}

std::string_view Parser::trim(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return sv;
}

std::string_view Parser::trim_trailing(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return sv;
}

bool Parser::is_comment_line(std::string_view content) {
    content = trim(content);
    if (content.empty()) return false;
    if (content[0] == '#') return true;
    if (content.size() >= 2 && content[0] == '/' && content[1] == '/') return true;
    return false;
}

void Parser::strip_trailing_comment(std::string_view& content) {
    if (!opts_.allow_comments) return;

    // Find # or // not inside a string
    bool in_string = false;
    bool escape = false;

    for (size_t i = 0; i < content.size(); i++) {
        char c = content[i];

        if (escape) {
            escape = false;
            continue;
        }

        if (c == '\\' && in_string) {
            escape = true;
            continue;
        }

        if (c == '"') {
            in_string = !in_string;
            continue;
        }

        if (!in_string) {
            if (c == '#') {
                // Check for preceding whitespace
                if (i > 0 && std::isspace(static_cast<unsigned char>(content[i-1]))) {
                    content = trim_trailing(content.substr(0, i));
                    return;
                }
            } else if (c == '/' && i + 1 < content.size() && content[i+1] == '/') {
                if (i > 0 && std::isspace(static_cast<unsigned char>(content[i-1]))) {
                    content = trim_trailing(content.substr(0, i));
                    return;
                }
            }
        }
    }
}

LineInfo Parser::classify_line(std::string_view line, size_t line_no) {
    LineInfo info;
    info.line_no = line_no;
    info.indent = count_indent(line);
    info.content = line.substr(info.indent);

    // Empty line
    if (info.content.empty()) {
        info.type = LineType::EMPTY;
        return info;
    }

    // Comment line
    if (opts_.allow_comments && is_comment_line(info.content)) {
        info.type = LineType::COMMENT;
        return info;
    }

    // Strip trailing comments for further processing
    std::string_view content = info.content;
    strip_trailing_comment(content);

    // List item: starts with "- "
    if (content.size() >= 2 && content[0] == '-' && content[1] == ' ') {
        info.type = LineType::LIST_ITEM;
        info.value = trim(content.substr(2));
        return info;
    }

    // Array header: starts with [
    if (!content.empty() && content[0] == '[') {
        info.tabular = parse_array_header(content);
        if (info.tabular.is_tabular) {
            info.type = LineType::TABULAR_HEADER;
        } else {
            info.type = LineType::ARRAY_HEADER;
        }
        return info;
    }

    // Key-value: contains ':'
    size_t colon_pos = std::string_view::npos;
    bool in_string = false;
    bool escape = false;

    for (size_t i = 0; i < content.size(); i++) {
        char c = content[i];

        if (escape) {
            escape = false;
            continue;
        }

        if (c == '\\' && in_string) {
            escape = true;
            continue;
        }

        if (c == '"') {
            in_string = !in_string;
            continue;
        }

        if (!in_string && c == ':') {
            colon_pos = i;
            break;
        }
    }

    if (colon_pos != std::string_view::npos) {
        info.key = trim(content.substr(0, colon_pos));
        std::string_view after_colon = content.substr(colon_pos + 1);

        // Remove key quotes if present
        if (info.key.size() >= 2 && info.key.front() == '"' && info.key.back() == '"') {
            info.key = info.key.substr(1, info.key.size() - 2);
        }

        after_colon = trim(after_colon);

        if (after_colon.empty()) {
            info.type = LineType::KEY_NESTED;
        } else {
            info.type = LineType::KEY_VALUE;
            info.value = after_colon;
        }
        return info;
    }

    // Raw value
    info.type = LineType::RAW_VALUE;
    info.value = trim(content);
    return info;
}

TabularHeader Parser::parse_array_header(std::string_view text) {
    TabularHeader header;

    // Format: [N]: or [N]{field1,field2,...}:
    if (text.empty() || text[0] != '[') {
        return header;
    }

    size_t pos = 1;

    // Parse N (optional number)
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        pos++;
    }

    if (pos > 1) {
        std::string_view num_str = text.substr(1, pos - 1);
        auto result = std::from_chars(num_str.data(), num_str.data() + num_str.size(), header.declared_count);
        (void)result;
    }

    // Expect ]
    if (pos >= text.size() || text[pos] != ']') {
        return header;
    }
    pos++;

    // Check for {fields}
    if (pos < text.size() && text[pos] == '{') {
        header.is_tabular = true;
        size_t field_start = pos + 1;
        size_t field_end = text.find('}', field_start);

        if (field_end != std::string_view::npos) {
            std::string_view fields_str = text.substr(field_start, field_end - field_start);

            // Parse comma-separated fields
            while (!fields_str.empty()) {
                size_t comma = fields_str.find(',');
                std::string_view field = (comma != std::string_view::npos)
                    ? fields_str.substr(0, comma)
                    : fields_str;
                field = trim(field);
                if (!field.empty()) {
                    header.fields.push_back(std::string(field));
                }
                if (comma != std::string_view::npos) {
                    fields_str = fields_str.substr(comma + 1);
                } else {
                    break;
                }
            }
            pos = field_end + 1;
        }
    }

    // Expect : at end
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        pos++;
    }
    if (pos < text.size() && text[pos] == ':') {
        // Valid header
    }

    return header;
}

bool Parser::parse_null(std::string_view text) {
    return text == "null";
}

std::optional<bool> Parser::parse_bool(std::string_view text) {
    if (text == "true") return true;
    if (text == "false") return false;
    return std::nullopt;
}

std::optional<int64_t> Parser::parse_integer(std::string_view text) {
    if (text.empty()) return std::nullopt;

    // Check if it's a valid integer (no decimal point)
    bool has_decimal = false;
    bool has_exp = false;
    for (char c : text) {
        if (c == '.') has_decimal = true;
        if (c == 'e' || c == 'E') has_exp = true;
    }

    if (has_decimal || has_exp) return std::nullopt;

    int64_t value;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);

    if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) {
        // Check if it fits in 32-bit signed integer
        // Note: INT32_MIN (-2147483648) is R's NA_integer_, so we exclude it
        // and let it be handled as a double instead
        if (value > INT32_MIN && value <= INT32_MAX) {
            return value;
        }
    }

    return std::nullopt;
}

std::optional<double> Parser::parse_number(std::string_view text) {
    if (text.empty()) return std::nullopt;

    // Try to parse as double
    double value;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);

    if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) {
        // Reject special values in strict mode
        if (opts_.strict) {
            if (std::isnan(value) || std::isinf(value)) {
                return std::nullopt;
            }
        }
        return value;
    }

    return std::nullopt;
}

std::optional<std::string> Parser::parse_quoted_string(std::string_view text) {
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') {
        return std::nullopt;
    }

    std::string result;
    result.reserve(text.size() - 2);

    for (size_t i = 1; i < text.size() - 1; i++) {
        char c = text[i];

        if (c == '\\' && i + 1 < text.size() - 1) {
            char next = text[i + 1];
            switch (next) {
                case '"':  result += '"'; i++; break;
                case '\\': result += '\\'; i++; break;
                case 'n':  result += '\n'; i++; break;
                case 'r':  result += '\r'; i++; break;
                case 't':  result += '\t'; i++; break;
                case 'u':  {
                    // Unicode escape \uXXXX
                    if (i + 5 < text.size() - 1) {
                        std::string_view hex = text.substr(i + 2, 4);
                        unsigned int cp;
                        auto res = std::from_chars(hex.data(), hex.data() + 4, cp, 16);
                        if (res.ec == std::errc{}) {
                            // Simple UTF-8 encoding for BMP
                            if (cp < 0x80) {
                                result += static_cast<char>(cp);
                            } else if (cp < 0x800) {
                                result += static_cast<char>(0xC0 | (cp >> 6));
                                result += static_cast<char>(0x80 | (cp & 0x3F));
                            } else {
                                result += static_cast<char>(0xE0 | (cp >> 12));
                                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                                result += static_cast<char>(0x80 | (cp & 0x3F));
                            }
                            i += 5;
                        } else {
                            if (opts_.strict) {
                                return std::nullopt;
                            }
                            result += c;
                        }
                    } else {
                        if (opts_.strict) {
                            return std::nullopt;
                        }
                        result += c;
                    }
                    break;
                }
                default:
                    if (opts_.strict) {
                        return std::nullopt; // Invalid escape sequence
                    }
                    result += c;
                    break;
            }
        } else {
            result += c;
        }
    }

    return result;
}

NodePtr Parser::parse_primitive(std::string_view text) {
    text = trim(text);

    if (text.empty()) {
        return nullptr;
    }

    // null
    if (parse_null(text)) {
        return Node::make_null();
    }

    // boolean
    auto bool_val = parse_bool(text);
    if (bool_val.has_value()) {
        return Node::make_bool(*bool_val);
    }

    // quoted string
    if (!text.empty() && text.front() == '"') {
        auto str_val = parse_quoted_string(text);
        if (str_val.has_value()) {
            return Node::make_string(*str_val);
        }
        // In non-strict mode, treat as unquoted string
        if (!opts_.strict) {
            return Node::make_string(text);
        }
        return nullptr;
    }

    // integer (fits in 32-bit)
    auto int_val = parse_integer(text);
    if (int_val.has_value()) {
        return Node::make_int(*int_val);
    }

    // double
    auto dbl_val = parse_number(text);
    if (dbl_val.has_value()) {
        return Node::make_double(*dbl_val);
    }

    // In non-strict mode, treat unrecognized as string
    if (!opts_.strict) {
        return Node::make_string(text);
    }

    return nullptr;
}

std::vector<std::string_view> Parser::parse_tabular_row(std::string_view line, char delimiter) {
    std::vector<std::string_view> fields;

    size_t start = 0;
    bool in_string = false;
    bool escape = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];

        if (escape) {
            escape = false;
            continue;
        }

        if (c == '\\' && in_string) {
            escape = true;
            continue;
        }

        if (c == '"') {
            in_string = !in_string;
            continue;
        }

        if (!in_string && c == delimiter) {
            fields.push_back(trim(line.substr(start, i - start)));
            start = i + 1;
        }
    }

    // Last field
    if (start <= line.size()) {
        fields.push_back(trim(line.substr(start)));
    }

    return fields;
}

void Parser::error(const std::string& msg, size_t line, size_t col) {
    throw ParseError(msg, line, col, "", current_file_);
}

std::string Parser::get_snippet(std::string_view line, size_t col) {
    std::string snippet(line);
    if (snippet.length() > 60) {
        snippet = snippet.substr(0, 57) + "...";
    }
    return snippet;
}

NodePtr Parser::parse_string(const std::string& text) {
    return parse_string(text.data(), text.size());
}

NodePtr Parser::parse_string(const char* data, size_t len) {
    warnings_.clear();
    current_file_.clear();
    has_peeked_ = false;

    BufferedReader reader(data, len);
    return parse_value(reader, -1);
}

NodePtr Parser::parse_file(const std::string& filepath) {
    warnings_.clear();
    current_file_ = filepath;
    has_peeked_ = false;

    BufferedReader reader(filepath);
    if (reader.has_error()) {
        throw ParseError(reader.error_message(), 0, 0, "", filepath);
    }

    return parse_value(reader, -1);
}

ValidationResult Parser::validate_string(const std::string& text) {
    try {
        auto node = parse_string(text);
        if (!node) {
            return ValidationResult::error("Failed to parse TOON");
        }
        return ValidationResult::ok();
    } catch (const ParseError& e) {
        return ValidationResult::error(e.what(), e.line(), e.column(), e.snippet(), e.file());
    }
}

ValidationResult Parser::validate_file(const std::string& filepath) {
    try {
        auto node = parse_file(filepath);
        if (!node) {
            return ValidationResult::error("Failed to parse TOON", 0, 0, "", filepath);
        }
        return ValidationResult::ok();
    } catch (const ParseError& e) {
        return ValidationResult::error(e.what(), e.line(), e.column(), e.snippet(), e.file());
    }
}

NodePtr Parser::parse_value(BufferedReader& reader, int parent_indent) {
    std::string_view line;
    size_t line_no;

    // Skip empty and comment lines
    while (true) {
        if (has_peeked_) {
            has_peeked_ = false;
            if (peeked_line_.type == LineType::EMPTY || peeked_line_.type == LineType::COMMENT) {
                continue;
            }
            // Use peeked line info
            LineInfo& info = peeked_line_;

            if (static_cast<int>(info.indent) <= parent_indent) {
                // Dedent - return null to signal end of nested block
                has_peeked_ = true;
                return nullptr;
            }

            switch (info.type) {
                case LineType::KEY_VALUE:
                case LineType::KEY_NESTED:
                    return parse_object(reader, parent_indent, info.key);

                case LineType::LIST_ITEM: {
                    auto arr = Node::make_array();
                    // Parse first item
                    if (!info.value.empty()) {
                        auto item_val = parse_primitive(info.value);
                        if (item_val) {
                            arr->array_items.push_back(item_val);
                        } else {
                            // May need to parse nested value
                            has_peeked_ = true;
                            peeked_line_.type = LineType::RAW_VALUE;
                            peeked_line_.value = info.value;
                            peeked_line_.indent = info.indent + 2;
                            auto nested = parse_value(reader, info.indent);
                            if (nested) {
                                arr->array_items.push_back(nested);
                            }
                        }
                    } else {
                        auto nested = parse_value(reader, info.indent);
                        if (nested) {
                            arr->array_items.push_back(nested);
                        }
                    }
                    // Continue parsing list items at same level
                    return parse_array(reader, parent_indent, TabularHeader());
                }

                case LineType::ARRAY_HEADER:
                case LineType::TABULAR_HEADER:
                    return parse_array(reader, parent_indent, info.tabular);

                case LineType::RAW_VALUE: {
                    auto prim = parse_primitive(info.value);
                    if (prim) return prim;
                    error("Invalid value: " + std::string(info.value), info.line_no);
                    return nullptr;
                }

                default:
                    return nullptr;
            }
        }

        if (!reader.next_line(line, line_no)) {
            return nullptr;
        }

        LineInfo info = classify_line(line, line_no);

        if (info.type == LineType::EMPTY || info.type == LineType::COMMENT) {
            continue;
        }

        if (static_cast<int>(info.indent) <= parent_indent) {
            // Dedent - save for parent
            has_peeked_ = true;
            peeked_line_ = info;
            peeked_raw_ = line;
            return nullptr;
        }

        switch (info.type) {
            case LineType::KEY_VALUE:
            case LineType::KEY_NESTED:
                // Set peeked_line_ so parse_object can access full line info
                peeked_line_ = info;
                return parse_object(reader, parent_indent, info.key);

            case LineType::LIST_ITEM: {
                auto arr = Node::make_array();
                int list_indent = info.indent;

                // Parse first item
                if (!info.value.empty()) {
                    auto item_val = parse_primitive(info.value);
                    if (item_val) {
                        arr->array_items.push_back(item_val);
                    } else {
                        // May be inline object/array start
                        // For simplicity, treat as string if not primitive
                        arr->array_items.push_back(Node::make_string(info.value));
                    }
                } else {
                    auto nested = parse_value(reader, info.indent);
                    if (nested) {
                        arr->array_items.push_back(nested);
                    } else {
                        arr->array_items.push_back(Node::make_null());
                    }
                }

                // Continue parsing list items at same indent level
                while (true) {
                    if (!reader.next_line(line, line_no)) break;

                    LineInfo next_info = classify_line(line, line_no);

                    if (next_info.type == LineType::EMPTY || next_info.type == LineType::COMMENT) {
                        continue;
                    }

                    if (static_cast<int>(next_info.indent) <= parent_indent) {
                        // Dedent back to parent
                        has_peeked_ = true;
                        peeked_line_ = next_info;
                        break;
                    }

                    if (next_info.type != LineType::LIST_ITEM || static_cast<int>(next_info.indent) != list_indent) {
                        has_peeked_ = true;
                        peeked_line_ = next_info;
                        break;
                    }

                    // Another list item
                    if (!next_info.value.empty()) {
                        auto item_val = parse_primitive(next_info.value);
                        if (item_val) {
                            arr->array_items.push_back(item_val);
                        } else {
                            arr->array_items.push_back(Node::make_string(next_info.value));
                        }
                    } else {
                        auto nested = parse_value(reader, next_info.indent);
                        if (nested) {
                            arr->array_items.push_back(nested);
                        } else {
                            arr->array_items.push_back(Node::make_null());
                        }
                    }
                }

                return arr;
            }

            case LineType::ARRAY_HEADER:
            case LineType::TABULAR_HEADER: {
                has_peeked_ = true;
                peeked_line_ = info;
                return parse_array(reader, parent_indent, info.tabular);
            }

            case LineType::RAW_VALUE: {
                auto prim = parse_primitive(info.value);
                if (prim) return prim;
                error("Invalid value: " + std::string(info.value), line_no);
                return nullptr;
            }

            default:
                return nullptr;
        }
    }
}

NodePtr Parser::parse_object(BufferedReader& reader, int parent_indent, std::string_view first_key) {
    auto obj = Node::make_object();
    std::unordered_set<std::string> seen_keys;
    std::unordered_map<std::string, int> duplicate_counts;

    std::string_view line;
    size_t line_no;

    // We already have the first key from the caller (via peeked_line_)
    // Re-read to get full context
    LineInfo info = peeked_line_;
    int obj_indent = info.indent;

    auto process_key_value = [&](const LineInfo& kv_info) {
        std::string key_str(kv_info.key);

        // Check for duplicates
        if (seen_keys.count(key_str)) {
            if (!opts_.allow_duplicate_keys) {
                error("Duplicate key: " + key_str, kv_info.line_no);
            } else if (opts_.warn) {
                duplicate_counts[key_str]++;
            }
            // Remove old entry (last-one-wins)
            for (auto it = obj->object_items.begin(); it != obj->object_items.end(); ++it) {
                if (it->first == key_str) {
                    obj->object_items.erase(it);
                    break;
                }
            }
        }
        seen_keys.insert(key_str);

        NodePtr value;
        if (kv_info.type == LineType::KEY_VALUE) {
            value = parse_primitive(kv_info.value);
            if (!value) {
                // Could be inline array or object start
                if (!kv_info.value.empty() && kv_info.value[0] == '[') {
                    // Inline array header
                    auto header = parse_array_header(kv_info.value);
                    if (header.declared_count > 0 || header.is_tabular) {
                        has_peeked_ = true;
                        peeked_line_.type = header.is_tabular ? LineType::TABULAR_HEADER : LineType::ARRAY_HEADER;
                        peeked_line_.tabular = header;
                        peeked_line_.indent = kv_info.indent + 2;
                        value = parse_array(reader, kv_info.indent, header);
                    } else {
                        value = Node::make_string(kv_info.value);
                    }
                } else {
                    value = Node::make_string(kv_info.value);
                }
            }
        } else {
            // KEY_NESTED
            value = parse_value(reader, kv_info.indent);
            if (!value) {
                value = Node::make_null();
            }
        }

        obj->object_items.push_back({key_str, value});
    };

    // Process first key
    process_key_value(info);

    // Continue parsing key-value pairs at same or dedented level
    while (true) {
        if (has_peeked_) {
            info = peeked_line_;
            has_peeked_ = false;
        } else {
            if (!reader.next_line(line, line_no)) break;
            info = classify_line(line, line_no);
        }

        if (info.type == LineType::EMPTY || info.type == LineType::COMMENT) {
            continue;
        }

        if (static_cast<int>(info.indent) <= parent_indent) {
            // Dedent to parent
            has_peeked_ = true;
            peeked_line_ = info;
            break;
        }

        if (static_cast<int>(info.indent) != obj_indent) {
            // Different indent level - could be nested content or dedent
            has_peeked_ = true;
            peeked_line_ = info;
            break;
        }

        if (info.type != LineType::KEY_VALUE && info.type != LineType::KEY_NESTED) {
            // Not a key-value pair
            has_peeked_ = true;
            peeked_line_ = info;
            break;
        }

        process_key_value(info);
    }

    // Emit duplicate key warnings
    if (opts_.warn && !duplicate_counts.empty()) {
        std::string warn_msg = "Duplicate keys found: ";
        bool first = true;
        for (const auto& [key, count] : duplicate_counts) {
            if (!first) warn_msg += ", ";
            warn_msg += key + " (" + std::to_string(count + 1) + " times)";
            first = false;
        }
        warnings_.push_back(Warning("duplicate_key", warn_msg));
    }

    return obj;
}

NodePtr Parser::parse_array(BufferedReader& reader, int parent_indent, const TabularHeader& header) {
    auto arr = Node::make_array();

    std::string_view line;
    size_t line_no;
    int arr_indent = -1;

    // Handle header line if present
    // Note: We don't set arr_indent from the header - arr_indent should be the indent of the actual items
    if (has_peeked_ && (peeked_line_.type == LineType::ARRAY_HEADER || peeked_line_.type == LineType::TABULAR_HEADER)) {
        // arr_indent will be set when we see the first actual item
        has_peeked_ = false;
    }

    // For tabular arrays, parse rows
    if (header.is_tabular) {
        size_t rows_seen = 0;

        while (true) {
            if (!reader.next_line(line, line_no)) break;

            LineInfo info = classify_line(line, line_no);

            if (info.type == LineType::EMPTY || info.type == LineType::COMMENT) {
                continue;
            }

            if (arr_indent < 0) {
                arr_indent = info.indent;
            }

            if (static_cast<int>(info.indent) <= parent_indent) {
                has_peeked_ = true;
                peeked_line_ = info;
                break;
            }

            if (static_cast<int>(info.indent) < arr_indent) {
                has_peeked_ = true;
                peeked_line_ = info;
                break;
            }

            // Parse row
            auto fields = parse_tabular_row(info.content, header.delimiter);
            auto row_obj = Node::make_object();

            for (size_t i = 0; i < fields.size() && i < header.fields.size(); i++) {
                auto field_val = parse_primitive(fields[i]);
                if (!field_val) {
                    field_val = Node::make_string(fields[i]);
                }
                row_obj->object_items.push_back({header.fields[i], field_val});
            }

            arr->array_items.push_back(row_obj);
            rows_seen++;
        }

        // Warn if row count mismatch
        if (opts_.warn && header.declared_count > 0 && rows_seen != header.declared_count) {
            std::string msg = "Declared [" + std::to_string(header.declared_count) +
                "] but observed " + std::to_string(rows_seen) + " rows; using observed.";
            warnings_.push_back(Warning("n_mismatch", msg));
        }
    } else {
        // Non-tabular array - parse list items
        while (true) {
            if (has_peeked_) {
                LineInfo info = peeked_line_;
                has_peeked_ = false;

                if (info.type == LineType::EMPTY || info.type == LineType::COMMENT) {
                    continue;
                }

                if (arr_indent < 0 && info.type == LineType::LIST_ITEM) {
                    arr_indent = info.indent;
                }

                if (static_cast<int>(info.indent) <= parent_indent) {
                    has_peeked_ = true;
                    peeked_line_ = info;
                    break;
                }

                if (info.type == LineType::LIST_ITEM && static_cast<int>(info.indent) == arr_indent) {
                    if (!info.value.empty()) {
                        auto item_val = parse_primitive(info.value);
                        if (item_val) {
                            arr->array_items.push_back(item_val);
                        } else {
                            arr->array_items.push_back(Node::make_string(info.value));
                        }
                    } else {
                        auto nested = parse_value(reader, info.indent);
                        arr->array_items.push_back(nested ? nested : Node::make_null());
                    }
                    continue;
                }

                has_peeked_ = true;
                peeked_line_ = info;
                break;
            }

            if (!reader.next_line(line, line_no)) break;

            LineInfo info = classify_line(line, line_no);

            if (info.type == LineType::EMPTY || info.type == LineType::COMMENT) {
                continue;
            }

            if (arr_indent < 0 && info.type == LineType::LIST_ITEM) {
                arr_indent = info.indent;
            }

            if (static_cast<int>(info.indent) <= parent_indent) {
                has_peeked_ = true;
                peeked_line_ = info;
                break;
            }

            if (info.type == LineType::LIST_ITEM && static_cast<int>(info.indent) == arr_indent) {
                if (!info.value.empty()) {
                    auto item_val = parse_primitive(info.value);
                    if (item_val) {
                        arr->array_items.push_back(item_val);
                    } else {
                        arr->array_items.push_back(Node::make_string(info.value));
                    }
                } else {
                    auto nested = parse_value(reader, info.indent);
                    arr->array_items.push_back(nested ? nested : Node::make_null());
                }
            } else {
                has_peeked_ = true;
                peeked_line_ = info;
                break;
            }
        }

        // Check declared count
        if (opts_.warn && header.declared_count > 0 && arr->array_items.size() != header.declared_count) {
            std::string msg = "Declared [" + std::to_string(header.declared_count) +
                "] but observed " + std::to_string(arr->array_items.size()) + " items; using observed.";
            warnings_.push_back(Warning("n_mismatch", msg));
        }
    }

    return arr;
}

} // namespace toonlite
