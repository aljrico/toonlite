#ifndef TOON_STREAM_HPP
#define TOON_STREAM_HPP

#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include "toon_errors.h"
#include "toon_io.h"
#include "toon_r.h"

namespace toonlite {

// Streaming writer for tabular data
class StreamWriter {
public:
    StreamWriter(const std::string& filepath, const std::vector<std::string>& schema, int indent = 2);
    ~StreamWriter();

    // Write a batch of rows
    void write_batch(SEXP df_batch);

    // Finalize and close
    void close();

private:
    std::string filepath_;
    std::string tmp_path_;
    std::vector<std::string> schema_;
    int indent_;
    std::ofstream file_;
    WriteBuffer buf_{64 * 1024};
    size_t rows_written_ = 0;
    bool closed_ = false;
};

} // namespace toonlite

#endif // TOON_STREAM_HPP
