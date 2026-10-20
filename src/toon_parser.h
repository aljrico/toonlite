#ifndef TOON_PARSER_H
#define TOON_PARSER_H

// TOON decoder (spec v4.4 sections 4-14) for read_toon(), from_toon(),
// validate_toon() and toon_info().
//
// The document is decoded into flat arrays instead of one heap object per
// value: nodes, child links and string bytes each live in one vector, and a
// tabular array keeps its cells in one block with its field list stored once.
//
// TOON values map to R as follows (document_to_r):
//   object        -> named list ({} is a named list of length 0)
//   array         -> list; with `simplify`, an array of primitives of one
//                    type becomes an atomic vector (null -> NA, integers mixed
//                    with doubles -> double; all-null arrays stay lists)
//   string        -> character(1)
//   number        -> integer(1) when it is an integer in R's integer range
//                    (-2147483647 to 2147483647), otherwise double(1); numbers
//                    beyond the double range become -Inf/Inf
//   true / false  -> logical(1)
//   null          -> NULL

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "toon_errors.h"
#include "toon_lex.h"
#include "toon_r.h"

namespace toonlite {

enum class NodeKind : uint8_t { Null, Bool, Int, Double, String, Array, Object, Table };

struct Node {
    NodeKind kind = NodeKind::Null;
    uint32_t len = 0;  // String: bytes; Array/Object: children; Table: rows
    union {
        int32_t i;
        double d;
        bool b;
        uint64_t off;  // String: pool offset; Array/Object: first child; Table: table index
    };
    Node() : off(0) {}
};

// A child of an array or object. Array children leave the key empty.
struct Kid {
    uint32_t node = 0;
    uint32_t key_len = 0;
    uint64_t key_off = 0;
};

// A tabular array or a keyed tabular object (spec 9.3, 9.5). Row r's cells
// are cells[cell_off + r * leaves ...]; a keyed table lists its entries in
// `keys` and the row each entry takes its values from in `rows` (they differ
// when non-strict decoding resolves duplicate entry keys).
struct Table {
    std::vector<ResolvedGroup> groups;
    size_t leaves = 0;
    uint64_t cell_off = 0;
    bool keyed = false;
    std::vector<Kid> keys;
    std::vector<uint64_t> rows;
};

struct Document {
    std::vector<Node> nodes;
    std::vector<Kid> kids;
    std::vector<Node> cells;
    std::vector<Table> tables;
    std::string pool;
    uint32_t root = 0;

    std::string_view str(uint64_t off, uint32_t len) const {
        return std::string_view(pool.data() + off, len);
    }
};

// Decode a whole document. Throws ParseError.
Document parse_toon(const char* data, size_t len, const ReadOptions& opts);
Document parse_toon_file(const std::string& path, const ReadOptions& opts);

// The R value of a decoded document (unprotected).
SEXP document_to_r(const Document& doc, bool simplify);

struct DocumentInfo {
    size_t arrays = 0;
    size_t objects = 0;
    bool has_tabular = false;
    size_t first_table_rows = 0;  // rows of the first tabular array
};
DocumentInfo document_info(const Document& doc);

// A cheap look at the start of a file: the root form, the first keys of a
// root object and the first lines as written. Nothing is validated.
struct PeekInfo {
    std::string type = "unknown";  // object, array, tabular_array, primitive
    std::vector<std::string> first_keys;
    std::vector<std::string> preview;
};
PeekInfo peek_toon_file(const std::string& path, size_t max_lines);

}  // namespace toonlite

#endif  // TOON_PARSER_H
