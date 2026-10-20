#ifndef TOON_IO_HPP
#define TOON_IO_HPP

#include <string>
#include <string_view>
#include <fstream>
#include <vector>
#include <cstddef>
#include <memory>
#include <optional>

namespace toonlite {

// Decode a double-quoted TOON string literal (quotes included). Handles
// \" \\ \n \r \t and \uXXXX (BMP). Returns nullopt if `text` is not quoted
// or, when `strict`, if it contains an invalid escape sequence. Shared by
// the DOM parser and the tabular column builder so both decode identically.
std::optional<std::string> unescape_quoted_string(std::string_view text, bool strict);

// Buffered line reader for efficient file I/O
class BufferedReader {
public:
    static constexpr size_t DEFAULT_BUFFER_SIZE = 4 * 1024 * 1024; // 4MB buffer

    BufferedReader(const std::string& filepath, size_t buffer_size = DEFAULT_BUFFER_SIZE);
    BufferedReader(const char* data, size_t length);

    // Returns false when no more lines available
    bool next_line(std::string_view& out_line, size_t& out_line_no);

    // Check for errors
    bool has_error() const { return has_error_; }
    const std::string& error_message() const { return error_message_; }

private:
    bool fill_buffer();
    void handle_crlf(std::string_view& line);

    std::ifstream file_;
    std::vector<char> buffer_;
    size_t buffer_size_;
    size_t buffer_pos_ = 0;
    size_t buffer_end_ = 0;
    size_t line_no_ = 0;
    bool eof_reached_ = false;
    bool has_error_ = false;
    std::string error_message_;

    // For string input
    const char* string_data_ = nullptr;
    size_t string_length_ = 0;
    size_t string_pos_ = 0;

    // Scratch buffer for lines spanning buffer boundaries
    std::string scratch_;
};

} // namespace toonlite

#endif // TOON_IO_HPP
