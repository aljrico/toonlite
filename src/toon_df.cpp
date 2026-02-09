#include "toon_df.h"
#include "toon_charconv.h"
#include <charconv>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace toonlite {

// ColBuilder implementation
ColBuilder::ColBuilder(const std::string& name, size_t initial_capacity)
    : name_(name), capacity_(initial_capacity) {
    na_mask_.reserve(initial_capacity);
}

void ColBuilder::ensure_capacity(size_t n) {
    if (n <= capacity_) return;

    size_t new_cap = std::max(n, capacity_ * 2);
    capacity_ = new_cap;

    switch (type_) {
        case ColType::LOGICAL:
            lgl_data_.reserve(new_cap);
            break;
        case ColType::INTEGER:
            int_data_.reserve(new_cap);
            break;
        case ColType::DOUBLE:
            dbl_data_.reserve(new_cap);
            break;
        case ColType::STRING:
            str_data_.reserve(new_cap);
            break;
        default:
            break;
    }
    na_mask_.reserve(new_cap);
}

void ColBuilder::promote_to(ColType new_type) {
    if (new_type == type_) return;

    switch (new_type) {
        case ColType::INTEGER:
            if (type_ == ColType::LOGICAL) {
                // Logical -> Integer
                int_data_.resize(size_);
                for (size_t i = 0; i < size_; i++) {
                    if (na_mask_[i]) {
                        int_data_[i] = NA_INTEGER;
                    } else {
                        int_data_[i] = lgl_data_[i];
                    }
                }
                lgl_data_.clear();
            }
            break;

        case ColType::DOUBLE:
            if (type_ == ColType::LOGICAL) {
                dbl_data_.resize(size_);
                for (size_t i = 0; i < size_; i++) {
                    if (na_mask_[i]) {
                        dbl_data_[i] = NA_REAL;
                    } else {
                        dbl_data_[i] = static_cast<double>(lgl_data_[i]);
                    }
                }
                lgl_data_.clear();
            } else if (type_ == ColType::INTEGER) {
                dbl_data_.resize(size_);
                for (size_t i = 0; i < size_; i++) {
                    if (na_mask_[i]) {
                        dbl_data_[i] = NA_REAL;
                    } else {
                        dbl_data_[i] = static_cast<double>(int_data_[i]);
                    }
                }
                int_data_.clear();
            }
            break;

        case ColType::STRING:
            str_data_.resize(size_);
            if (type_ == ColType::LOGICAL) {
                for (size_t i = 0; i < size_; i++) {
                    if (na_mask_[i]) {
                        str_data_[i] = "";  // NA marker
                    } else {
                        str_data_[i] = lgl_data_[i] ? "true" : "false";
                    }
                }
                lgl_data_.clear();
            } else if (type_ == ColType::INTEGER) {
                for (size_t i = 0; i < size_; i++) {
                    if (na_mask_[i]) {
                        str_data_[i] = "";
                    } else {
                        str_data_[i] = std::to_string(int_data_[i]);
                    }
                }
                int_data_.clear();
            } else if (type_ == ColType::DOUBLE) {
                for (size_t i = 0; i < size_; i++) {
                    if (na_mask_[i]) {
                        str_data_[i] = "";
                    } else {
                        str_data_[i] = std::to_string(dbl_data_[i]);
                    }
                }
                dbl_data_.clear();
            }
            break;

        default:
            break;
    }

    type_ = new_type;
}

void ColBuilder::force_type(ColType t) {
    if (type_ == ColType::UNKNOWN) {
        type_ = t;
    } else if (type_ != t) {
        promote_to(t);
    }
}

void ColBuilder::set_null(size_t row) {
    ensure_capacity(row + 1);

    // Extend size if needed
    while (size_ <= row) {
        na_mask_.push_back(true);
        switch (type_) {
            case ColType::LOGICAL: lgl_data_.push_back(NA_LOGICAL); break;
            case ColType::INTEGER: int_data_.push_back(NA_INTEGER); break;
            case ColType::DOUBLE: dbl_data_.push_back(NA_REAL); break;
            case ColType::STRING: str_data_.push_back(""); break;
            case ColType::UNKNOWN:
                // First value is null - defer type decision
                lgl_data_.push_back(NA_LOGICAL);
                break;
        }
        size_++;
    }

    na_mask_[row] = true;
    switch (type_) {
        case ColType::LOGICAL: lgl_data_[row] = NA_LOGICAL; break;
        case ColType::INTEGER: int_data_[row] = NA_INTEGER; break;
        case ColType::DOUBLE: dbl_data_[row] = NA_REAL; break;
        case ColType::STRING: str_data_[row] = ""; break;
        case ColType::UNKNOWN: lgl_data_[row] = NA_LOGICAL; break;
    }
}

void ColBuilder::parse_and_store(size_t row, std::string_view value) {
    // Trim whitespace
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }

    // Check for null
    if (value == "null") {
        set_null(row);
        return;
    }

    // Check for boolean
    if (value == "true") {
        if (type_ == ColType::UNKNOWN) {
            type_ = ColType::LOGICAL;
            lgl_data_.resize(row + 1, NA_LOGICAL);
            na_mask_.resize(row + 1, true);
        } else if (type_ != ColType::LOGICAL) {
            if (type_ == ColType::INTEGER) {
                promote_to(ColType::INTEGER);
                int_data_.resize(row + 1, NA_INTEGER);
                int_data_[row] = 1;
                na_mask_[row] = false;
                if (row >= size_) size_ = row + 1;
                return;
            } else if (type_ == ColType::DOUBLE) {
                dbl_data_.resize(row + 1, NA_REAL);
                dbl_data_[row] = 1.0;
                na_mask_[row] = false;
                if (row >= size_) size_ = row + 1;
                return;
            } else {
                promote_to(ColType::STRING);
                str_data_.resize(row + 1, "");
                str_data_[row] = "true";
                na_mask_[row] = false;
                if (row >= size_) size_ = row + 1;
                return;
            }
        }
        lgl_data_.resize(row + 1, NA_LOGICAL);
        na_mask_.resize(row + 1, true);
        lgl_data_[row] = 1;
        na_mask_[row] = false;
        if (row >= size_) size_ = row + 1;
        return;
    }

    if (value == "false") {
        if (type_ == ColType::UNKNOWN) {
            type_ = ColType::LOGICAL;
            lgl_data_.resize(row + 1, NA_LOGICAL);
            na_mask_.resize(row + 1, true);
        } else if (type_ != ColType::LOGICAL) {
            if (type_ == ColType::INTEGER) {
                int_data_.resize(row + 1, NA_INTEGER);
                int_data_[row] = 0;
                na_mask_[row] = false;
                if (row >= size_) size_ = row + 1;
                return;
            } else if (type_ == ColType::DOUBLE) {
                dbl_data_.resize(row + 1, NA_REAL);
                dbl_data_[row] = 0.0;
                na_mask_[row] = false;
                if (row >= size_) size_ = row + 1;
                return;
            } else {
                promote_to(ColType::STRING);
                str_data_.resize(row + 1, "");
                str_data_[row] = "false";
                na_mask_[row] = false;
                if (row >= size_) size_ = row + 1;
                return;
            }
        }
        lgl_data_.resize(row + 1, NA_LOGICAL);
        na_mask_.resize(row + 1, true);
        lgl_data_[row] = 0;
        na_mask_[row] = false;
        if (row >= size_) size_ = row + 1;
        return;
    }

    // Check for quoted string
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        std::string unquoted;
        unquoted.reserve(value.size() - 2);

        for (size_t i = 1; i < value.size() - 1; i++) {
            if (value[i] == '\\' && i + 1 < value.size() - 1) {
                char next = value[i + 1];
                switch (next) {
                    case '"': unquoted += '"'; i++; break;
                    case '\\': unquoted += '\\'; i++; break;
                    case 'n': unquoted += '\n'; i++; break;
                    case 'r': unquoted += '\r'; i++; break;
                    case 't': unquoted += '\t'; i++; break;
                    default: unquoted += value[i]; break;
                }
            } else {
                unquoted += value[i];
            }
        }

        promote_to(ColType::STRING);
        str_data_.resize(row + 1, "");
        na_mask_.resize(row + 1, true);
        str_data_[row] = std::move(unquoted);
        na_mask_[row] = false;
        if (row >= size_) size_ = row + 1;
        return;
    }

    // Try to parse as integer
    bool has_decimal = false;
    bool has_exp = false;
    for (char c : value) {
        if (c == '.') has_decimal = true;
        if (c == 'e' || c == 'E') has_exp = true;
    }

    if (!has_decimal && !has_exp) {
        int64_t int_val;
        auto result = std::from_chars(value.data(), value.data() + value.size(), int_val);
        if (result.ec == std::errc{} && result.ptr == value.data() + value.size()) {
            if (int_val >= INT32_MIN && int_val <= INT32_MAX) {
                if (type_ == ColType::UNKNOWN) {
                    type_ = ColType::INTEGER;
                    int_data_.resize(row + 1, NA_INTEGER);
                    na_mask_.resize(row + 1, true);
                } else if (type_ == ColType::LOGICAL) {
                    promote_to(ColType::INTEGER);
                    int_data_.resize(row + 1, NA_INTEGER);
                    na_mask_.resize(row + 1, true);
                } else if (type_ == ColType::DOUBLE) {
                    dbl_data_.resize(row + 1, NA_REAL);
                    na_mask_.resize(row + 1, true);
                    dbl_data_[row] = static_cast<double>(int_val);
                    na_mask_[row] = false;
                    if (row >= size_) size_ = row + 1;
                    return;
                } else if (type_ == ColType::STRING) {
                    str_data_.resize(row + 1, "");
                    na_mask_.resize(row + 1, true);
                    str_data_[row] = std::string(value);
                    na_mask_[row] = false;
                    if (row >= size_) size_ = row + 1;
                    return;
                }

                int_data_.resize(row + 1, NA_INTEGER);
                na_mask_.resize(row + 1, true);
                int_data_[row] = static_cast<int>(int_val);
                na_mask_[row] = false;
                if (row >= size_) size_ = row + 1;
                return;
            }
        }
    }

    // Try to parse as double
    double dbl_val;
    auto result = double_from_chars(value.data(), value.data() + value.size(), dbl_val);
    if (result.ec == std::errc{} && result.ptr == value.data() + value.size()) {
        if (type_ == ColType::UNKNOWN || type_ == ColType::LOGICAL || type_ == ColType::INTEGER) {
            promote_to(ColType::DOUBLE);
            dbl_data_.resize(row + 1, NA_REAL);
            na_mask_.resize(row + 1, true);
        } else if (type_ == ColType::STRING) {
            str_data_.resize(row + 1, "");
            na_mask_.resize(row + 1, true);
            str_data_[row] = std::string(value);
            na_mask_[row] = false;
            if (row >= size_) size_ = row + 1;
            return;
        }

        dbl_data_.resize(row + 1, NA_REAL);
        na_mask_.resize(row + 1, true);
        dbl_data_[row] = dbl_val;
        na_mask_[row] = false;
        if (row >= size_) size_ = row + 1;
        return;
    }

    // Treat as string (unquoted)
    promote_to(ColType::STRING);
    str_data_.resize(row + 1, "");
    na_mask_.resize(row + 1, true);
    str_data_[row] = std::string(value);
    na_mask_[row] = false;
    if (row >= size_) size_ = row + 1;
}

void ColBuilder::set(size_t row, std::string_view value) {
    ensure_capacity(row + 1);
    parse_and_store(row, value);
}

SEXP ColBuilder::finalize() {
    SEXP result;

    // Default to logical if still unknown
    if (type_ == ColType::UNKNOWN) {
        type_ = ColType::LOGICAL;
    }

    switch (type_) {
        case ColType::LOGICAL: {
            result = PROTECT(Rf_allocVector(LGLSXP, size_));
            int* out = LOGICAL(result);
            for (size_t i = 0; i < size_; i++) {
                out[i] = na_mask_[i] ? NA_LOGICAL : lgl_data_[i];
            }
            UNPROTECT(1);
            break;
        }
        case ColType::INTEGER: {
            result = PROTECT(Rf_allocVector(INTSXP, size_));
            int* out = INTEGER(result);
            for (size_t i = 0; i < size_; i++) {
                out[i] = na_mask_[i] ? NA_INTEGER : int_data_[i];
            }
            UNPROTECT(1);
            break;
        }
        case ColType::DOUBLE: {
            result = PROTECT(Rf_allocVector(REALSXP, size_));
            double* out = REAL(result);
            for (size_t i = 0; i < size_; i++) {
                out[i] = na_mask_[i] ? NA_REAL : dbl_data_[i];
            }
            UNPROTECT(1);
            break;
        }
        case ColType::STRING: {
            result = PROTECT(Rf_allocVector(STRSXP, size_));
            for (size_t i = 0; i < size_; i++) {
                if (na_mask_[i]) {
                    SET_STRING_ELT(result, i, NA_STRING);
                } else {
                    SET_STRING_ELT(result, i, Rf_mkCharCE(str_data_[i].c_str(), CE_UTF8));
                }
            }
            UNPROTECT(1);
            break;
        }
        default:
            result = R_NilValue;
            break;
    }

    return result;
}

// TabularParser implementation
TabularParser::TabularParser(const TabularParseOptions& opts)
    : opts_(opts) {}

std::string_view TabularParser::trim(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return sv;
}

bool TabularParser::parse_header(std::string_view header) {
    header = trim(header);

    // Format: [N]{field1,field2,...}:
    if (header.empty() || header[0] != '[') {
        return false;
    }

    size_t pos = 1;

    // Parse N
    while (pos < header.size() && std::isdigit(static_cast<unsigned char>(header[pos]))) {
        pos++;
    }

    if (pos > 1) {
        auto result = std::from_chars(header.data() + 1, header.data() + pos, declared_rows_);
        (void)result;
    }

    // Expect ]
    if (pos >= header.size() || header[pos] != ']') {
        return false;
    }
    pos++;

    // Must have {fields}
    if (pos >= header.size() || header[pos] != '{') {
        return false;
    }
    pos++;

    // Find closing }
    size_t field_end = header.find('}', pos);
    if (field_end == std::string_view::npos) {
        return false;
    }

    std::string_view fields_str = header.substr(pos, field_end - pos);

    // Parse comma-separated fields
    while (!fields_str.empty()) {
        size_t comma = fields_str.find(',');
        std::string_view field = (comma != std::string_view::npos)
            ? fields_str.substr(0, comma)
            : fields_str;
        field = trim(field);
        if (!field.empty()) {
            field_names_.push_back(std::string(field));
        }
        if (comma != std::string_view::npos) {
            fields_str = fields_str.substr(comma + 1);
        } else {
            break;
        }
    }

    // Create column builders
    for (const auto& name : field_names_) {
        columns_.emplace_back(name, std::max(size_t(1000), declared_rows_));
    }

    // Apply user-specified column types
    for (const auto& [name, type] : opts_.col_types) {
        for (auto& col : columns_) {
            if (col.name() == name) {
                col.force_type(type);
                break;
            }
        }
    }

    return !field_names_.empty();
}

std::vector<std::string_view> TabularParser::split_row(std::string_view line, char delimiter) {
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

void TabularParser::parse_row_line(std::string_view line, size_t line_no) {
    auto fields = split_row(line, delimiter_);
    size_t n_fields = fields.size();

    // Track min/max fields
    if (n_fields < min_fields_) min_fields_ = n_fields;
    if (n_fields > max_fields_) max_fields_ = n_fields;

    // Handle ragged rows
    if (n_fields != columns_.size()) {
        if (opts_.ragged_rows == "error") {
            throw ParseError("Row has " + std::to_string(n_fields) + " fields but expected " +
                std::to_string(columns_.size()), line_no, 0, "", current_file_);
        }

        // expand_warn mode
        if (n_fields > columns_.size()) {
            // Expand schema
            size_t extra = n_fields - columns_.size();
            if (schema_expansions_ + extra > opts_.max_extra_cols) {
                throw ParseError("max_extra_cols exceeded", line_no, 0, "", current_file_);
            }

            for (size_t i = columns_.size(); i < n_fields; i++) {
                std::string new_name = "V" + std::to_string(i + 1);
                columns_.emplace_back(new_name, std::max(size_t(1000), declared_rows_));
                field_names_.push_back(new_name);

                // Backfill with NA
                for (size_t r = 0; r < observed_rows_; r++) {
                    columns_.back().set_null(r);
                }
            }
            schema_expansions_ += extra;
        }
    }

    // Store values
    size_t row = observed_rows_;
    for (size_t i = 0; i < columns_.size(); i++) {
        if (i < n_fields) {
            columns_[i].set(row, fields[i]);
        } else {
            columns_[i].set_null(row);
        }
    }

    observed_rows_++;
}

bool TabularParser::find_tabular_array(BufferedReader& reader, std::string& header_line, size_t& header_line_no) {
    std::string_view line;
    size_t line_no;

    // If key is specified, need to find root[key] first
    if (opts_.key.has_value()) {
        std::string target_key = opts_.key.value();
        bool found_key = false;

        while (reader.next_line(line, line_no)) {
            // Skip empty lines and comments
            auto trimmed = trim(line);
            if (trimmed.empty()) continue;
            if (opts_.allow_comments && (trimmed[0] == '#' ||
                (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/'))) {
                continue;
            }

            // Look for "key:"
            size_t colon = trimmed.find(':');
            if (colon != std::string_view::npos) {
                auto key = trim(trimmed.substr(0, colon));
                if (key == target_key) {
                    found_key = true;
                    // Check if value is inline
                    auto value = trim(trimmed.substr(colon + 1));
                    if (!value.empty() && value[0] == '[') {
                        header_line = std::string(value);
                        header_line_no = line_no;
                        return true;
                    }
                    // Value is on next line(s)
                    break;
                }
            }
        }

        if (!found_key) {
            throw ParseError("Key not found: " + target_key, 0, 0, "", current_file_);
        }
    }

    // Find tabular header
    while (reader.next_line(line, line_no)) {
        auto trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (opts_.allow_comments && (trimmed[0] == '#' ||
            (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/'))) {
            continue;
        }

        // Check for tabular array header [N]{...}:
        if (trimmed[0] == '[') {
            // Find { and }
            auto brace_start = trimmed.find('{');
            auto brace_end = trimmed.find('}');
            if (brace_start != std::string_view::npos && brace_end != std::string_view::npos) {
                header_line = std::string(trimmed);
                header_line_no = line_no;
                return true;
            }
        }
    }

    return false;
}

void TabularParser::parse_rows(BufferedReader& reader, int base_indent) {
    std::string_view line;
    size_t line_no;

    while (reader.next_line(line, line_no)) {
        // Count indentation
        size_t indent = 0;
        for (char c : line) {
            if (c == ' ') {
                indent++;
            } else if (c == '\t') {
                if (opts_.strict) {
                    throw ParseError("Tab characters not allowed in indentation", line_no);
                }
                indent++;
            } else {
                break;
            }
        }

        auto content = line.substr(indent);
        content = trim(content);

        // Skip empty/comment lines
        if (content.empty()) continue;
        if (opts_.allow_comments && (content[0] == '#' ||
            (content.size() >= 2 && content[0] == '/' && content[1] == '/'))) {
            continue;
        }

        // Check for dedent (end of tabular array)
        if (base_indent >= 0 && static_cast<int>(indent) <= base_indent) {
            break;
        }

        // Parse row
        // Strip trailing comment
        if (opts_.allow_comments) {
            bool in_string = false;
            for (size_t i = 0; i < content.size(); i++) {
                char c = content[i];
                if (c == '"') in_string = !in_string;
                if (!in_string && c == '#') {
                    content = trim(content.substr(0, i));
                    break;
                }
            }
        }

        parse_row_line(content, line_no);
    }
}

SEXP TabularParser::parse_file(const std::string& filepath) {
    warnings_.clear();
    current_file_ = filepath;
    field_names_.clear();
    columns_.clear();
    declared_rows_ = 0;
    observed_rows_ = 0;
    min_fields_ = SIZE_MAX;
    max_fields_ = 0;
    schema_expansions_ = 0;

    BufferedReader reader(filepath);
    if (reader.has_error()) {
        throw ParseError(reader.error_message(), 0, 0, "", filepath);
    }

    std::string header_line;
    size_t header_line_no;

    if (!find_tabular_array(reader, header_line, header_line_no)) {
        throw ParseError("No tabular array found", 0, 0, "", filepath);
    }

    if (!parse_header(header_line)) {
        throw ParseError("Invalid tabular header", header_line_no, 0, header_line, filepath);
    }

    parse_rows(reader, -1);

    // Check row count mismatch
    if (declared_rows_ > 0 && observed_rows_ != declared_rows_) {
        if (opts_.n_mismatch == "error") {
            throw ParseError("Declared [" + std::to_string(declared_rows_) + "] but observed " +
                std::to_string(observed_rows_) + " rows", 0, 0, "", filepath);
        } else if (opts_.warn) {
            warnings_.push_back(Warning("n_mismatch",
                "Declared [" + std::to_string(declared_rows_) + "] but observed " +
                std::to_string(observed_rows_) + " rows; using observed."));
        }
    }

    // Check ragged rows
    if (min_fields_ != max_fields_ && opts_.warn) {
        std::string msg = "Tabular rows had inconsistent field counts (min=" +
            std::to_string(min_fields_) + ", max=" + std::to_string(max_fields_) + ").";
        if (schema_expansions_ > 0) {
            msg += " Schema expanded to " + std::to_string(columns_.size()) + " columns;";
        }
        msg += " missing values filled with NA.";
        warnings_.push_back(Warning("ragged_rows", msg));
    }

    return build_dataframe(columns_, observed_rows_);
}

SEXP TabularParser::parse_string(const char* data, size_t len) {
    warnings_.clear();
    current_file_.clear();
    field_names_.clear();
    columns_.clear();
    declared_rows_ = 0;
    observed_rows_ = 0;
    min_fields_ = SIZE_MAX;
    max_fields_ = 0;
    schema_expansions_ = 0;

    BufferedReader reader(data, len);

    std::string header_line;
    size_t header_line_no;

    if (!find_tabular_array(reader, header_line, header_line_no)) {
        throw ParseError("No tabular array found");
    }

    if (!parse_header(header_line)) {
        throw ParseError("Invalid tabular header", header_line_no, 0, header_line, "");
    }

    parse_rows(reader, -1);

    // Check warnings
    if (declared_rows_ > 0 && observed_rows_ != declared_rows_) {
        if (opts_.n_mismatch == "error") {
            throw ParseError("Declared [" + std::to_string(declared_rows_) + "] but observed " +
                std::to_string(observed_rows_) + " rows");
        } else if (opts_.warn) {
            warnings_.push_back(Warning("n_mismatch",
                "Declared [" + std::to_string(declared_rows_) + "] but observed " +
                std::to_string(observed_rows_) + " rows; using observed."));
        }
    }

    if (min_fields_ != max_fields_ && opts_.warn) {
        std::string msg = "Tabular rows had inconsistent field counts (min=" +
            std::to_string(min_fields_) + ", max=" + std::to_string(max_fields_) + ").";
        if (schema_expansions_ > 0) {
            msg += " Schema expanded to " + std::to_string(columns_.size()) + " columns;";
        }
        msg += " missing values filled with NA.";
        warnings_.push_back(Warning("ragged_rows", msg));
    }

    return build_dataframe(columns_, observed_rows_);
}

SEXP build_dataframe(std::vector<ColBuilder>& columns, size_t nrow) {
    size_t ncol = columns.size();

    SEXP df = PROTECT(Rf_allocVector(VECSXP, ncol));
    SEXP names = PROTECT(Rf_allocVector(STRSXP, ncol));

    for (size_t i = 0; i < ncol; i++) {
        SET_VECTOR_ELT(df, i, columns[i].finalize());
        SET_STRING_ELT(names, i, Rf_mkCharCE(columns[i].name().c_str(), CE_UTF8));
    }

    Rf_setAttrib(df, R_NamesSymbol, names);

    // Set row.names
    SEXP row_names = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(row_names)[0] = NA_INTEGER;
    INTEGER(row_names)[1] = -static_cast<int>(nrow);
    Rf_setAttrib(df, R_RowNamesSymbol, row_names);

    // Set class
    SEXP class_attr = PROTECT(Rf_allocVector(STRSXP, 1));
    SET_STRING_ELT(class_attr, 0, Rf_mkChar("data.frame"));
    Rf_setAttrib(df, R_ClassSymbol, class_attr);

    UNPROTECT(4);
    return df;
}

} // namespace toonlite
