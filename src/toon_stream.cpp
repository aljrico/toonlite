#include "toon_stream.hpp"
#include <charconv>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace toonlite {

std::string_view RowStreamer::trim(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return sv;
}

RowStreamer::RowStreamer(const std::string& filepath, const StreamOptions& opts)
    : filepath_(filepath), opts_(opts) {
    reader_ = std::make_unique<BufferedReader>(filepath);
    if (reader_->has_error()) {
        throw ParseError(reader_->error_message(), 0, 0, "", filepath);
    }
}

bool RowStreamer::parse_header(std::string_view header) {
    header = trim(header);

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

    return !field_names_.empty();
}

bool RowStreamer::find_tabular_header() {
    std::string_view line;
    size_t line_no;

    // If key is specified, find it first
    if (opts_.key.has_value()) {
        std::string target_key = opts_.key.value();
        bool found_key = false;

        while (reader_->next_line(line, line_no)) {
            auto trimmed = trim(line);
            if (trimmed.empty()) continue;
            if (opts_.allow_comments && (trimmed[0] == '#' ||
                (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/'))) {
                continue;
            }

            size_t colon = trimmed.find(':');
            if (colon != std::string_view::npos) {
                auto key = trim(trimmed.substr(0, colon));
                if (key == target_key) {
                    found_key = true;
                    auto value = trim(trimmed.substr(colon + 1));
                    if (!value.empty() && value[0] == '[') {
                        if (parse_header(value)) {
                            return true;
                        }
                    }
                    break;
                }
            }
        }

        if (!found_key) {
            throw ParseError("Key not found: " + target_key, 0, 0, "", filepath_);
        }
    }

    // Find tabular header
    while (reader_->next_line(line, line_no)) {
        auto trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (opts_.allow_comments && (trimmed[0] == '#' ||
            (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/'))) {
            continue;
        }

        if (trimmed[0] == '[') {
            auto brace_start = trimmed.find('{');
            auto brace_end = trimmed.find('}');
            if (brace_start != std::string_view::npos && brace_end != std::string_view::npos) {
                if (parse_header(trimmed)) {
                    return true;
                }
            }
        }
    }

    return false;
}

std::vector<std::string_view> RowStreamer::split_row(std::string_view line, char delimiter) {
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

    if (start <= line.size()) {
        fields.push_back(trim(line.substr(start)));
    }

    return fields;
}

void RowStreamer::process_batch() {
    // Build data.frame from batch_columns_
    SEXP df = build_dataframe(batch_columns_, batch_rows_);

    // Reset batch
    batch_columns_.clear();
    for (const auto& name : field_names_) {
        batch_columns_.emplace_back(name, opts_.batch_size);
    }
    batch_rows_ = 0;

    (void)df; // Used by caller
}

void RowStreamer::stream(SEXP callback) {
    if (!find_tabular_header()) {
        throw ParseError("No tabular array found", 0, 0, "", filepath_);
    }

    // Initialize batch columns
    for (const auto& name : field_names_) {
        batch_columns_.emplace_back(name, opts_.batch_size);
    }

    // Apply user-specified column types
    for (const auto& [name, type] : opts_.col_types) {
        for (auto& col : batch_columns_) {
            if (col.name() == name) {
                col.force_type(type);
                break;
            }
        }
    }

    std::string_view line;
    size_t line_no;
    size_t check_interrupt_counter = 0;

    while (reader_->next_line(line, line_no)) {
        // Count indentation
        size_t indent = 0;
        for (char c : line) {
            if (c == ' ') indent++;
            else if (c == '\t') indent++;
            else break;
        }

        auto content = line.substr(indent);
        content = trim(content);

        if (content.empty()) continue;
        if (opts_.allow_comments && (content[0] == '#' ||
            (content.size() >= 2 && content[0] == '/' && content[1] == '/'))) {
            continue;
        }

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

        // Parse row
        auto fields = split_row(content, delimiter_);
        size_t n_fields = fields.size();

        if (n_fields < min_fields_) min_fields_ = n_fields;
        if (n_fields > max_fields_) max_fields_ = n_fields;

        // Handle ragged rows
        if (n_fields != batch_columns_.size()) {
            if (opts_.ragged_rows == "error") {
                throw ParseError("Row has " + std::to_string(n_fields) + " fields but expected " +
                    std::to_string(batch_columns_.size()), line_no, 0, "", filepath_);
            }

            if (n_fields > batch_columns_.size()) {
                size_t extra = n_fields - batch_columns_.size();
                if (schema_expansions_ + extra > opts_.max_extra_cols) {
                    throw ParseError("max_extra_cols exceeded", line_no, 0, "", filepath_);
                }

                for (size_t i = batch_columns_.size(); i < n_fields; i++) {
                    std::string new_name = "V" + std::to_string(i + 1);
                    batch_columns_.emplace_back(new_name, opts_.batch_size);
                    field_names_.push_back(new_name);

                    for (size_t r = 0; r < batch_rows_; r++) {
                        batch_columns_.back().set_null(r);
                    }
                }
                schema_expansions_ += extra;
            }
        }

        // Store values
        for (size_t i = 0; i < batch_columns_.size(); i++) {
            if (i < n_fields) {
                batch_columns_[i].set(batch_rows_, fields[i]);
            } else {
                batch_columns_[i].set_null(batch_rows_);
            }
        }

        batch_rows_++;
        observed_rows_++;

        // Emit batch if full
        if (batch_rows_ >= opts_.batch_size) {
            SEXP df = PROTECT(build_dataframe(batch_columns_, batch_rows_));

            // Call R callback
            SEXP call = PROTECT(Rf_lang2(callback, df));
            R_tryEval(call, R_GlobalEnv, NULL);
            UNPROTECT(2);

            // Reset batch
            batch_columns_.clear();
            for (const auto& name : field_names_) {
                batch_columns_.emplace_back(name, opts_.batch_size);
            }
            batch_rows_ = 0;
        }

        // Check for user interrupt
        if (++check_interrupt_counter >= 10000) {
            R_CheckUserInterrupt();
            check_interrupt_counter = 0;
        }
    }

    // Emit final batch if any rows remain
    if (batch_rows_ > 0) {
        SEXP df = PROTECT(build_dataframe(batch_columns_, batch_rows_));
        SEXP call = PROTECT(Rf_lang2(callback, df));
        R_tryEval(call, R_GlobalEnv, NULL);
        UNPROTECT(2);
    }

    // Check row count mismatch
    if (declared_rows_ > 0 && observed_rows_ != declared_rows_) {
        if (opts_.n_mismatch == "error") {
            throw ParseError("Declared [" + std::to_string(declared_rows_) + "] but observed " +
                std::to_string(observed_rows_) + " rows", 0, 0, "", filepath_);
        } else if (opts_.warn) {
            warnings_.push_back(Warning("n_mismatch",
                "Declared [" + std::to_string(declared_rows_) + "] but observed " +
                std::to_string(observed_rows_) + " rows; using observed."));
        }
    }

    // Ragged row warning
    if (min_fields_ != max_fields_ && opts_.warn) {
        std::string msg = "Tabular rows had inconsistent field counts (min=" +
            std::to_string(min_fields_) + ", max=" + std::to_string(max_fields_) + ").";
        if (schema_expansions_ > 0) {
            msg += " Schema expanded to " + std::to_string(field_names_.size()) + " columns;";
        }
        msg += " missing values filled with NA.";
        warnings_.push_back(Warning("ragged_rows", msg));
    }
}

// ItemStreamer implementation
ItemStreamer::ItemStreamer(const std::string& filepath, const StreamOptions& opts)
    : filepath_(filepath), opts_(opts) {}

void ItemStreamer::stream(SEXP callback) {
    // Parse as general TOON and stream array items
    // For simplicity, use the general parser
    BufferedReader reader(filepath_);
    if (reader.has_error()) {
        throw ParseError(reader.error_message(), 0, 0, "", filepath_);
    }

    // This is a simplified implementation - full implementation would
    // parse and stream items incrementally
    // For now, throw not implemented
    throw ParseError("Item streaming not fully implemented in this version", 0, 0, "", filepath_);
}

// StreamWriter implementation
StreamWriter::StreamWriter(const std::string& filepath, const std::vector<std::string>& schema, int indent)
    : filepath_(filepath), schema_(schema), indent_(indent) {
    file_.open(filepath, std::ios::binary);
    if (!file_.is_open()) {
        throw ParseError("Cannot open file for writing: " + filepath, 0, 0, "", filepath);
    }
}

StreamWriter::~StreamWriter() {
    if (!closed_) {
        try {
            close();
        } catch (...) {
            // Ignore errors in destructor
        }
    }
}

void StreamWriter::write_header() {
    if (header_written_) return;

    // Write placeholder header - we'll update the count at close
    file_ << "[0]{";
    for (size_t i = 0; i < schema_.size(); i++) {
        if (i > 0) file_ << ",";
        file_ << schema_[i];
    }
    file_ << "}:\n";

    header_written_ = true;
}

void StreamWriter::write_row(SEXP df, R_xlen_t row) {
    R_xlen_t ncol = Rf_xlength(df);

    for (int i = 0; i < indent_; i++) {
        file_ << ' ';
    }

    for (R_xlen_t j = 0; j < ncol; j++) {
        if (j > 0) file_ << ", ";

        SEXP col = VECTOR_ELT(df, j);

        switch (TYPEOF(col)) {
            case LGLSXP: {
                int val = LOGICAL(col)[row];
                if (val == NA_LOGICAL) {
                    file_ << "null";
                } else {
                    file_ << (val ? "true" : "false");
                }
                break;
            }
            case INTSXP: {
                int val = INTEGER(col)[row];
                if (val == NA_INTEGER) {
                    file_ << "null";
                } else {
                    file_ << val;
                }
                break;
            }
            case REALSXP: {
                double val = REAL(col)[row];
                if (ISNA(val) || ISNAN(val)) {
                    file_ << "null";
                } else {
                    file_ << std::setprecision(17) << val;
                }
                break;
            }
            case STRSXP: {
                SEXP elem = STRING_ELT(col, row);
                if (elem == NA_STRING) {
                    file_ << "null";
                } else {
                    // Write escaped string
                    file_ << '"';
                    const char* s = CHAR(elem);
                    while (*s) {
                        switch (*s) {
                            case '"': file_ << "\\\""; break;
                            case '\\': file_ << "\\\\"; break;
                            case '\n': file_ << "\\n"; break;
                            case '\r': file_ << "\\r"; break;
                            case '\t': file_ << "\\t"; break;
                            default: file_ << *s; break;
                        }
                        s++;
                    }
                    file_ << '"';
                }
                break;
            }
            default:
                file_ << "null";
                break;
        }
    }

    file_ << '\n';
    rows_written_++;
}

void StreamWriter::write_batch(SEXP df_batch) {
    if (!header_written_) {
        write_header();
    }

    R_xlen_t nrow = Rf_xlength(VECTOR_ELT(df_batch, 0));
    for (R_xlen_t i = 0; i < nrow; i++) {
        write_row(df_batch, i);
    }
}

void StreamWriter::close() {
    if (closed_) return;

    file_.close();

    // Reopen and update the header with actual row count
    std::ifstream in(filepath_, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    // Find and replace [0] with actual count
    size_t pos = content.find("[0]");
    if (pos != std::string::npos) {
        std::string count_str = "[" + std::to_string(rows_written_) + "]";
        content.replace(pos, 3, count_str);
    }

    std::ofstream out(filepath_, std::ios::binary);
    out << content;
    out.close();

    closed_ = true;
}

} // namespace toonlite
