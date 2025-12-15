#ifndef TOON_ERRORS_HPP
#define TOON_ERRORS_HPP

#include <string>
#include <stdexcept>
#include <cstddef>

namespace toonlite {

// Error types
enum class ErrorType {
    PARSE_ERROR,
    VALIDATION_ERROR,
    IO_ERROR,
    TYPE_ERROR,
    ENCODING_ERROR
};

// Parse error with location information
class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& message,
               size_t line = 0,
               size_t column = 0,
               const std::string& snippet = "",
               const std::string& file = "")
        : std::runtime_error(message),
          line_(line),
          column_(column),
          snippet_(snippet),
          file_(file) {}

    size_t line() const { return line_; }
    size_t column() const { return column_; }
    const std::string& snippet() const { return snippet_; }
    const std::string& file() const { return file_; }

    std::string formatted_message() const {
        std::string msg = what();
        if (!file_.empty()) {
            msg += "\n  File: " + file_;
        }
        if (line_ > 0) {
            msg += "\n  Location: line " + std::to_string(line_);
            if (column_ > 0) {
                msg += ", column " + std::to_string(column_);
            }
        }
        if (!snippet_.empty()) {
            msg += "\n  Snippet: " + snippet_;
        }
        return msg;
    }

private:
    size_t line_;
    size_t column_;
    std::string snippet_;
    std::string file_;
};

// Validation result (does not throw)
struct ValidationResult {
    bool valid = true;
    ErrorType error_type = ErrorType::PARSE_ERROR;
    std::string message;
    size_t line = 0;
    size_t column = 0;
    std::string snippet;
    std::string file;

    static ValidationResult ok() {
        return ValidationResult{true, ErrorType::PARSE_ERROR, "", 0, 0, "", ""};
    }

    static ValidationResult error(const std::string& msg,
                                   size_t line = 0,
                                   size_t col = 0,
                                   const std::string& snippet = "",
                                   const std::string& file = "") {
        return ValidationResult{false, ErrorType::PARSE_ERROR, msg, line, col, snippet, file};
    }
};

// Warning information for aggregated warnings
struct Warning {
    std::string type;
    std::string message;

    Warning(const std::string& t, const std::string& m) : type(t), message(m) {}
};

} // namespace toonlite

#endif // TOON_ERRORS_HPP
