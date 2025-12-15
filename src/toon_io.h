#ifndef TOON_IO_HPP
#define TOON_IO_HPP

#include <string>
#include <string_view>
#include <fstream>
#include <vector>
#include <cstddef>
#include <memory>

namespace toonlite {

// Buffered line reader for efficient file I/O
class BufferedReader {
public:
    static constexpr size_t DEFAULT_BUFFER_SIZE = 4 * 1024 * 1024; // 4MB buffer

    BufferedReader(const std::string& filepath, size_t buffer_size = DEFAULT_BUFFER_SIZE);
    BufferedReader(const char* data, size_t length);

    // Returns false when no more lines available
    bool next_line(std::string_view& out_line, size_t& out_line_no);

    // Get current line number (1-indexed)
    size_t current_line() const { return line_no_; }

    // Check if reading from file
    bool is_file() const { return file_.is_open(); }

    // Get file path (empty if reading from string)
    const std::string& filepath() const { return filepath_; }

    // Check for errors
    bool has_error() const { return has_error_; }
    const std::string& error_message() const { return error_message_; }

private:
    bool fill_buffer();
    void handle_crlf(std::string_view& line);

    std::ifstream file_;
    std::string filepath_;
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

// Write buffer for efficient output
class WriteBuffer {
public:
    static constexpr size_t DEFAULT_CAPACITY = 1024 * 1024; // 1MB

    WriteBuffer(size_t initial_capacity = DEFAULT_CAPACITY);

    void append(const char* data, size_t len);
    void append(const std::string& s);
    void append(std::string_view sv);
    void append_char(char c);

    // Append with proper TOON string escaping
    void append_escaped_string(std::string_view s);

    // Get current content
    std::string_view view() const;
    std::string str() const;

    // Write to file
    bool write_to_file(const std::string& filepath);

    void clear();
    size_t size() const { return data_.size(); }

private:
    std::vector<char> data_;
};

} // namespace toonlite

#endif // TOON_IO_HPP
