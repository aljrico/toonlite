#include "toon_io.h"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>

namespace toonlite {

std::optional<std::string> unescape_quoted_string(std::string_view text, bool strict) {
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
                            if (strict) {
                                return std::nullopt;
                            }
                            result += c;
                        }
                    } else {
                        if (strict) {
                            return std::nullopt;
                        }
                        result += c;
                    }
                    break;
                }
                default:
                    if (strict) {
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

BufferedReader::BufferedReader(const std::string& filepath, size_t buffer_size)
    : buffer_size_(buffer_size) {
    buffer_.resize(buffer_size_);
    file_.open(filepath, std::ios::binary);
    if (!file_.is_open()) {
        has_error_ = true;
        error_message_ = "Cannot open file: " + filepath;
    }
}

BufferedReader::BufferedReader(const char* data, size_t length)
    : buffer_size_(0), string_data_(data), string_length_(length) {
}

bool BufferedReader::fill_buffer() {
    if (eof_reached_) return false;

    // Move remaining data to beginning of buffer
    if (buffer_pos_ < buffer_end_) {
        size_t remaining = buffer_end_ - buffer_pos_;
        std::memmove(buffer_.data(), buffer_.data() + buffer_pos_, remaining);
        buffer_end_ = remaining;
        buffer_pos_ = 0;
    } else {
        buffer_pos_ = 0;
        buffer_end_ = 0;
    }

    if (!file_.is_open()) {
        eof_reached_ = true;
        return buffer_end_ > 0;
    }

    // Read more data
    file_.read(buffer_.data() + buffer_end_, buffer_size_ - buffer_end_);
    size_t bytes_read = static_cast<size_t>(file_.gcount());
    buffer_end_ += bytes_read;

    if (bytes_read == 0 || file_.eof()) {
        eof_reached_ = true;
    }

    return buffer_end_ > 0;
}

void BufferedReader::handle_crlf(std::string_view& line) {
    // Strip trailing \r if present (CRLF handling)
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
}

bool BufferedReader::next_line(std::string_view& out_line, size_t& out_line_no) {
    // Handle string input
    if (string_data_ != nullptr) {
        if (string_pos_ >= string_length_) {
            return false;
        }

        const char* start = string_data_ + string_pos_;
        const char* end = string_data_ + string_length_;
        const char* newline = std::find(start, end, '\n');

        size_t line_len = (newline != end) ? (newline - start) : (end - start);
        out_line = std::string_view(start, line_len);
        handle_crlf(out_line);

        string_pos_ = (newline != end) ? (newline - string_data_ + 1) : string_length_;
        line_no_++;
        out_line_no = line_no_;
        return true;
    }

    // Handle file input
    scratch_.clear();

    while (true) {
        // Need more data?
        if (buffer_pos_ >= buffer_end_) {
            if (!fill_buffer()) {
                // No more data - if we have scratch content, return it
                if (!scratch_.empty()) {
                    out_line = std::string_view(scratch_);
                    handle_crlf(out_line);
                    line_no_++;
                    out_line_no = line_no_;
                    return true;
                }
                return false;
            }
        }

        // Find newline in buffer
        const char* buf_start = buffer_.data() + buffer_pos_;
        const char* buf_end = buffer_.data() + buffer_end_;
        const char* newline = std::find(buf_start, buf_end, '\n');

        if (newline != buf_end) {
            // Found newline
            size_t line_len = newline - buf_start;

            if (scratch_.empty()) {
                // Line fits in buffer - return view directly
                out_line = std::string_view(buf_start, line_len);
            } else {
                // Append to scratch and return
                scratch_.append(buf_start, line_len);
                out_line = std::string_view(scratch_);
            }

            handle_crlf(out_line);
            buffer_pos_ = (newline - buffer_.data()) + 1;
            line_no_++;
            out_line_no = line_no_;
            return true;
        } else {
            // No newline found - append to scratch and read more
            scratch_.append(buf_start, buf_end - buf_start);
            buffer_pos_ = buffer_end_;
        }
    }
}

} // namespace toonlite
