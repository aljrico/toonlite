#include "toon_stream.h"
#include "toon_encoder.h"

#include <cstdio>
#include <fstream>

namespace toonlite {

// StreamWriter implementation
//
// The row count goes in the header, before the rows, and is only known at the
// end. Rows are therefore written to a temporary file next to the target; on
// close the header is written to the target and the rows are copied after it.
StreamWriter::StreamWriter(const std::string& filepath, const std::vector<std::string>& schema, int indent)
    : filepath_(filepath), tmp_path_(filepath + ".toonlite-rows"), schema_(schema), indent_(indent) {
    file_.open(tmp_path_, std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) {
        throw ParseError("Cannot open file for writing: " + tmp_path_, 0, 0, "", filepath);
    }
}

StreamWriter::~StreamWriter() {
    if (!closed_) {
        file_.close();
        std::remove(tmp_path_.c_str());
    }
}

void StreamWriter::write_batch(SEXP df_batch) {
    // Rows are formatted by the same cell writer as write_toon_df(), so a
    // streamed file reads back exactly like one written in a single call.
    // Non-finite doubles become null.
    R_xlen_t ncol = Rf_xlength(df_batch);
    R_xlen_t nrow = Rf_xlength(VECTOR_ELT(df_batch, 0));
    buf_.clear();
    for (R_xlen_t i = 0; i < nrow; i++) {
        for (int k = 0; k < indent_; k++) {
            buf_.append_char(' ');
        }
        for (R_xlen_t j = 0; j < ncol; j++) {
            if (j > 0) buf_.append(", ", 2);
            append_tabular_cell(buf_, VECTOR_ELT(df_batch, j), i, false);
        }
        buf_.append_char('\n');
    }

    file_.write(buf_.view().data(), static_cast<std::streamsize>(buf_.size()));
    if (!file_) {
        throw ParseError("Error writing to file: " + tmp_path_, 0, 0, "", filepath_);
    }
    rows_written_ += static_cast<size_t>(nrow);
}

void StreamWriter::close() {
    if (closed_) return;
    closed_ = true;
    file_.close();

    std::ofstream out(filepath_, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::remove(tmp_path_.c_str());
        throw ParseError("Cannot open file for writing: " + filepath_, 0, 0, "", filepath_);
    }
    out << '[' << rows_written_ << "]{";
    for (size_t i = 0; i < schema_.size(); i++) {
        if (i > 0) out << ',';
        out << schema_[i];
    }
    out << "}:\n";

    std::ifstream in(tmp_path_, std::ios::binary);
    std::vector<char> chunk(1 << 20);
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        out.write(chunk.data(), in.gcount());
    }
    in.close();
    std::remove(tmp_path_.c_str());
    if (!out) {
        throw ParseError("Error writing to file: " + filepath_, 0, 0, "", filepath_);
    }
}

} // namespace toonlite
