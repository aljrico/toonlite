#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "toon_parser.h"

namespace toonlite {

namespace {

constexpr size_t kLinearKeys = 16;

uint32_t len32(size_t n) {
    if (n > UINT32_MAX) throw std::runtime_error("Value too large to decode");
    return static_cast<uint32_t>(n);
}

// Finds an existing sibling key among kids[start, end): a linear scan for
// small objects, a hash map once an object grows past kLinearKeys members.
class KeyIndex {
public:
    explicit KeyIndex(size_t start) : start_(start) {}

    size_t find(const Document& d, const std::vector<Kid>& kids, std::string_view key) const {
        if (map_) {
            const auto it = map_->find(std::string(key));
            return it == map_->end() ? npos : it->second;
        }
        for (size_t i = start_; i < kids.size(); i++) {
            if (d.str(kids[i].key_off, kids[i].key_len) == key) return i;
        }
        return npos;
    }

    // Record kids.back() as a new member.
    void added(const Document& d, const std::vector<Kid>& kids) {
        if (map_) {
            const Kid& k = kids.back();
            map_->emplace(std::string(d.str(k.key_off, k.key_len)), kids.size() - 1);
        } else if (kids.size() - start_ > kLinearKeys) {
            map_ = std::make_unique<std::unordered_map<std::string, size_t>>();
            for (size_t i = start_; i < kids.size(); i++) {
                map_->emplace(std::string(d.str(kids[i].key_off, kids[i].key_len)), i);
            }
        }
    }

private:
    size_t start_;
    std::unique_ptr<std::unordered_map<std::string, size_t>> map_;
};

std::string duplicate_key_message(std::string_view key) {
    return "Duplicate key '" + std::string(key) + "'";
}

// Recursive-descent decoder over the shared lexer. Depths: a scope expects
// its first line at its nominal content depth c; a deeper first line is a
// depth jump (an error, or adopted as the scope's actual depth m in
// non-strict mode, spec 14.4). A scope ends at the first line shallower than
// c; lines between c and m, and lines deeper than m that open no nested
// scope, are errors (spec 8).
class Parser {
public:
    Parser(BufferedReader& src, const ReadOptions& opts, std::string file)
        : lines_(src, opts, std::move(file)), strict_(opts.strict) {}

    Document parse() {
        try {
            doc_.root = document();
        } catch (const LexError& e) {
            lines_.fail(e.what(), at_no_, at_text_);
        }
        return std::move(doc_);
    }

private:
    // An object under construction: its members sit on stack_ from `mark`.
    struct Object {
        size_t mark;
        KeyIndex keys;
        explicit Object(size_t m) : mark(m), keys(m) {}
    };

    // The line being decoded, for LexError locations.
    void at(const Line& L) {
        at_no_ = L.no;
        at_text_ = L.content;
    }

    void enter() {
        if (++level_ > kMaxNesting) {
            throw LexError("Nesting is deeper than " + std::to_string(kMaxNesting) + " levels");
        }
    }
    void leave() { level_--; }

    uint32_t add_node(const Node& n) {
        if (doc_.nodes.size() >= UINT32_MAX) throw std::runtime_error("Too many values to decode");
        doc_.nodes.push_back(n);
        return static_cast<uint32_t>(doc_.nodes.size() - 1);
    }

    Node make_scalar(std::string_view tok) {
        const Scalar s = parse_scalar(tok, scratch_);
        Node n;
        switch (s.kind) {
            case ScalarKind::Null:
                break;
            case ScalarKind::Bool:
                n.kind = NodeKind::Bool;
                n.b = s.b;
                break;
            case ScalarKind::Int:
                n.kind = NodeKind::Int;
                n.i = s.i;
                break;
            case ScalarKind::Double:
                n.kind = NodeKind::Double;
                n.d = s.d;
                break;
            case ScalarKind::String:
                n.kind = NodeKind::String;
                n.len = len32(s.str.size());
                n.off = doc_.pool.size();
                doc_.pool.append(s.str.data(), s.str.size());
                break;
        }
        return n;
    }

    uint32_t empty(NodeKind kind) {
        Node n;
        n.kind = kind;
        n.off = doc_.kids.size();
        return add_node(n);
    }

    // Close the container whose children sit on stack_ from `mark`.
    uint32_t finish(NodeKind kind, size_t mark) {
        Node n;
        n.kind = kind;
        n.len = len32(stack_.size() - mark);
        n.off = doc_.kids.size();
        doc_.kids.insert(doc_.kids.end(), stack_.begin() + static_cast<std::ptrdiff_t>(mark),
                         stack_.end());
        stack_.resize(mark);
        return add_node(n);
    }

    // Reserve the member slot for `key` (spec 14.3): a new slot, or in
    // non-strict mode the slot of an earlier duplicate, whose value the new
    // one replaces while keeping its position.
    size_t member(Object& ob, std::string_view key) {
        const size_t found = ob.keys.find(doc_, stack_, key);
        if (found != npos) {
            if (strict_) throw LexError(duplicate_key_message(key));
            return found;
        }
        Kid k;
        k.key_len = len32(key.size());
        k.key_off = doc_.pool.size();
        doc_.pool.append(key.data(), key.size());
        stack_.push_back(k);
        ob.keys.added(doc_, stack_);
        return stack_.size() - 1;
    }

    // Decode a key token (spec 7.4) into a view valid until the next scalar.
    std::string_view key_of(std::string_view tok) {
        if (!tok.empty() && tok[0] == '"') {
            unescape_quoted(tok, scratch_);
            return scratch_;
        }
        return tok;
    }

    // ---------------------------------------------------------------------
    // Root form (spec 5)
    // ---------------------------------------------------------------------

    uint32_t document() {
        const Line* L = lines_.peek();
        if (!L) return empty(NodeKind::Object);
        if (L->depth > 0) lines_.fail("The first line of a document must not be indented", L);
        at(*L);
        const std::string_view s = L->content;

        if (s == "[]") {
            lines_.consume();
            const uint32_t v = empty(NodeKind::Array);
            check_trailing("Unexpected content after the root array");
            return v;
        }
        const LineScan sc = scan_line(s);
        if (is_header_line(sc) && sc.bracket == 0) {
            Header h;
            parse_header(s, sc, strict_, h);
            h.line_no = L->no;
            lines_.consume();
            const uint32_t v = array_body(h, 1);
            check_trailing(h.keyed ? "Unexpected content after the root keyed table"
                                   : "Unexpected content after the root array");
            return v;
        }
        if (sc.colon == npos) {
            // A scalar line is a root primitive only as the sole line.
            lines_.consume();
            const size_t no = L->no;
            const std::string text(s);
            const uint32_t v = add_node(make_scalar(s));
            if (lines_.peek()) {
                lines_.fail("Expected 'key: value'; a lone value is only valid as a one-line document",
                            no, text);
            }
            return v;
        }
        Object ob(stack_.size());
        object_fields(0, 0, ob);
        return finish(NodeKind::Object, ob.mark);
    }

    void check_trailing(const char* msg) {
        if (const Line* L = lines_.peek()) lines_.fail(msg, L);
    }

    // ---------------------------------------------------------------------
    // Objects (spec 8, 10)
    // ---------------------------------------------------------------------

    // Fields standing at depth m, up to the first line shallower than c.
    void object_fields(size_t c, size_t m, Object& ob) {
        while (const Line* L = lines_.peek()) {
            if (L->depth < c) return;
            if (L->depth < m) lines_.fail("Line is shallower than the fields above it", L);
            if (L->depth > m) lines_.fail("Unexpected indentation", L);
            lines_.consume();
            at(*L);
            field(L->content, scan_line(L->content), m, ob);
        }
    }

    // One field line standing at depth fd.
    void field(std::string_view s, const LineScan& sc, size_t fd, Object& ob) {
        if (is_header_line(sc)) {
            Header h;
            parse_header(s, sc, strict_, h);
            h.line_no = at_no_;
            if (!h.has_key) {
                throw LexError("An array header without a key is only valid at the document root or after a list-item marker");
            }
            const size_t slot = member(ob, h.key);
            const uint32_t v = array_body(h, fd + 1);
            stack_[slot].node = v;
            return;
        }
        if (sc.colon == npos) throw LexError("Expected 'key: value'");
        const size_t slot = member(ob, key_of(trim_spaces(s.substr(0, sc.colon))));
        const std::string_view value = trim_spaces(s.substr(sc.colon + 1));
        uint32_t v;
        if (value.empty()) {
            v = object_scope(fd);
        } else if (value == "[]") {
            v = empty(NodeKind::Array);
        } else {
            v = add_node(make_scalar(value));
        }
        stack_[slot].node = v;
    }

    // The nested object opened by `key:` standing at depth fd.
    uint32_t object_scope(size_t fd) {
        const size_t c = fd + 1;
        Object ob(stack_.size());
        const Line* L = lines_.peek();
        if (L && L->depth >= c) {
            size_t m = c;
            if (L->depth > c) {
                if (strict_) lines_.fail("Indentation jumps more than one level", L);
                m = L->depth;
            }
            enter();
            object_fields(c, m, ob);
            leave();
        }
        return finish(NodeKind::Object, ob.mark);
    }

    // ---------------------------------------------------------------------
    // Arrays (spec 9)
    // ---------------------------------------------------------------------

    // The value a header opens; its content belongs at depth c.
    uint32_t array_body(const Header& h, size_t c) {
        if (h.has_fields) return table(h, c);
        if (!h.inline_values.empty()) return inline_array(h);
        return list_items(h, c);
    }

    uint32_t inline_array(const Header& h) {
        split_delimited(h.inline_values, h.delim, cells_);
        if (strict_ && !h.count_matches(cells_.size())) {
            throw LexError(count_mismatch_message(h, cells_.size(), "value"));
        }
        const size_t mark = stack_.size();
        for (const std::string_view tok : cells_) {
            Kid k;
            k.node = add_node(make_scalar(tok));
            stack_.push_back(k);
        }
        return finish(NodeKind::Array, mark);
    }

    uint32_t list_items(const Header& h, size_t c) {
        const size_t mark = stack_.size();
        size_t n = 0;
        const Line* L = lines_.peek();
        if (L && L->depth >= c) {
            size_t m = c;
            if (L->depth > c) {
                if (strict_) lines_.fail("Indentation jumps more than one level", L);
                m = L->depth;
            }
            enter();
            bool started = false;
            while ((L = lines_.peek()) != nullptr) {
                if (L->depth < c) break;
                if (L->depth < m) lines_.fail("Line is shallower than the list items above it", L);
                if (L->depth > m) lines_.fail("Unexpected indentation", L);
                std::string_view rest;
                if (!list_item_rest(L->content, rest)) {
                    lines_.fail("Expected a list item ('- value')", L);
                }
                lines_.consume();
                if (!started) {
                    lines_.span_started();
                    started = true;
                }
                at(*L);
                Kid k;
                k.node = list_item(rest, m);
                stack_.push_back(k);
                if ((++n & 0xFFFF) == 0) check_interrupt();
            }
            if (started) lines_.span_ended();
            leave();
        }
        if (strict_ && !h.count_matches(n)) {
            lines_.fail(count_mismatch_message(h, n, "list item"), h.line_no);
        }
        return finish(NodeKind::Array, mark);
    }

    // The item after a hyphen standing at depth d (spec 9.4, 10).
    uint32_t list_item(std::string_view rest, size_t d) {
        if (rest.empty()) return empty(NodeKind::Object);
        if (rest == "[]") return empty(NodeKind::Array);
        const LineScan sc = scan_line(rest);
        const bool header = is_header_line(sc);
        if (header && sc.bracket == 0) {
            // A keyless header is the item itself: its items sit at d + 1.
            Header h;
            parse_header(rest, sc, strict_, h);
            h.line_no = at_no_;
            if (h.has_fields) {
                throw LexError("A header with a field list needs a key unless it is the document root");
            }
            return array_body(h, d + 1);
        }
        if (!header && sc.colon == npos) return add_node(make_scalar(rest));

        // An object whose first field stands on the hyphen line, at d + 1.
        Object ob(stack_.size());
        enter();
        field(rest, sc, d + 1, ob);
        object_fields(d + 1, d + 1, ob);
        leave();
        return finish(NodeKind::Object, ob.mark);
    }

    // Tabular array or keyed tabular object; rows at depth c (spec 9.3, 9.5).
    uint32_t table(const Header& h, size_t c) {
        const size_t ti = doc_.tables.size();
        doc_.tables.emplace_back();
        Table& t = doc_.tables.back();
        t.groups = resolve_fields(h.fields);
        t.leaves = h.fields.leaf_count;
        t.cell_off = doc_.cells.size();
        t.keyed = h.keyed;

        RowScanner rows(lines_, h, c);
        std::string_view key_tok;
        KeyIndex keys(0);
        size_t n = 0;
        while (rows.next(cells_, &key_tok)) {
            at_no_ = rows.line_no();
            at_text_ = rows.line();
            for (const std::string_view tok : cells_) doc_.cells.push_back(make_scalar(tok));
            if (h.keyed) {
                const std::string_view key = key_of(key_tok);
                const size_t found = keys.find(doc_, t.keys, key);
                if (found == npos) {
                    Kid k;
                    k.key_len = len32(key.size());
                    k.key_off = doc_.pool.size();
                    doc_.pool.append(key.data(), key.size());
                    t.keys.push_back(k);
                    keys.added(doc_, t.keys);
                    t.rows.push_back(n);
                } else if (strict_) {
                    throw LexError(duplicate_key_message(key));
                } else {
                    t.rows[found] = n;
                }
            }
            if ((++n & 0xFFFF) == 0) check_interrupt();
        }
        if (strict_ && !h.count_matches(n)) {
            lines_.fail(count_mismatch_message(h, n, h.keyed ? "entry" : "row"), h.line_no);
        }
        Node node;
        node.kind = NodeKind::Table;
        node.len = len32(h.keyed ? t.rows.size() : n);
        node.off = ti;
        return add_node(node);
    }

    LineReader lines_;
    bool strict_;
    Document doc_;
    std::vector<Kid> stack_;
    std::vector<std::string_view> cells_;
    std::string scratch_;
    size_t level_ = 0;
    size_t at_no_ = 0;
    std::string_view at_text_;
};

// ---------------------------------------------------------------------------
// R values
// ---------------------------------------------------------------------------

class RBuilder {
public:
    RBuilder(const Document& d, bool simplify) : d_(d), simplify_(simplify) {}

    SEXP value(uint32_t id) {
        const Node& n = d_.nodes[id];
        switch (n.kind) {
            case NodeKind::Array: return array(n);
            case NodeKind::Object: return object(n);
            case NodeKind::Table: return table(n);
            default: return scalar(n);
        }
    }

private:
    SEXP str(uint64_t off, uint32_t len) const { return mkchar_utf8(d_.str(off, len)); }

    SEXP scalar(const Node& n) const {
        switch (n.kind) {
            case NodeKind::Bool: return Rf_ScalarLogical(n.b ? TRUE : FALSE);
            case NodeKind::Int: return Rf_ScalarInteger(n.i);
            case NodeKind::Double: return Rf_ScalarReal(n.d);
            case NodeKind::String: {
                SEXP out = PROTECT(Rf_allocVector(STRSXP, 1));
                SET_STRING_ELT(out, 0, str(n.off, n.len));
                UNPROTECT(1);
                return out;
            }
            default: return R_NilValue;
        }
    }

    // The atomic type an array of primitives simplifies to, or NILSXP.
    SEXPTYPE simple_type(const Node& n) const {
        bool lgl = false, num = false, dbl = false, chr = false;
        for (uint32_t i = 0; i < n.len; i++) {
            switch (d_.nodes[d_.kids[n.off + i].node].kind) {
                case NodeKind::Null: break;
                case NodeKind::Bool: lgl = true; break;
                case NodeKind::Int: num = true; break;
                case NodeKind::Double: num = dbl = true; break;
                case NodeKind::String: chr = true; break;
                default: return NILSXP;
            }
        }
        if (lgl + num + chr != 1) return NILSXP;  // mixed, or all null
        return lgl ? LGLSXP : chr ? STRSXP : dbl ? REALSXP : INTSXP;
    }

    SEXP array(const Node& n) {
        const SEXPTYPE type = simplify_ && n.len > 0 ? simple_type(n) : NILSXP;
        if (type != NILSXP) {
            SEXP out = PROTECT(Rf_allocVector(type, n.len));
            for (uint32_t i = 0; i < n.len; i++) {
                const Node& v = d_.nodes[d_.kids[n.off + i].node];
                const bool na = v.kind == NodeKind::Null;
                switch (type) {
                    case LGLSXP: LOGICAL(out)[i] = na ? NA_LOGICAL : (v.b ? TRUE : FALSE); break;
                    case INTSXP: INTEGER(out)[i] = na ? NA_INTEGER : v.i; break;
                    case REALSXP:
                        REAL(out)[i] = na ? NA_REAL : (v.kind == NodeKind::Int ? v.i : v.d);
                        break;
                    default: SET_STRING_ELT(out, i, na ? NA_STRING : str(v.off, v.len)); break;
                }
            }
            UNPROTECT(1);
            return out;
        }
        SEXP out = PROTECT(Rf_allocVector(VECSXP, n.len));
        for (uint32_t i = 0; i < n.len; i++) {
            SET_VECTOR_ELT(out, i, value(d_.kids[n.off + i].node));
        }
        UNPROTECT(1);
        return out;
    }

    SEXP object(const Node& n) {
        SEXP out = PROTECT(Rf_allocVector(VECSXP, n.len));
        SEXP names = Rf_allocVector(STRSXP, n.len);
        Rf_setAttrib(out, R_NamesSymbol, names);
        for (uint32_t i = 0; i < n.len; i++) {
            const Kid& k = d_.kids[n.off + i];
            SET_STRING_ELT(names, i, str(k.key_off, k.key_len));
            SET_VECTOR_ELT(out, i, value(k.node));
        }
        UNPROTECT(1);
        return out;
    }

    // One row object of field group g. `names` holds each group's names,
    // shared by every row.
    SEXP row(const Table& t, size_t g, uint64_t r, SEXP names) {
        const ResolvedGroup& grp = t.groups[g];
        const R_xlen_t k = static_cast<R_xlen_t>(grp.keys.size());
        SEXP out = PROTECT(Rf_allocVector(VECSXP, k));
        Rf_setAttrib(out, R_NamesSymbol, VECTOR_ELT(names, static_cast<R_xlen_t>(g)));
        const Node* cells = d_.cells.data() + t.cell_off + r * t.leaves;
        for (R_xlen_t j = 0; j < k; j++) {
            if (grp.leaf[j] >= 0) {
                SET_VECTOR_ELT(out, j, scalar(cells[grp.leaf[j]]));
            } else {
                SET_VECTOR_ELT(out, j, row(t, static_cast<size_t>(grp.group[j]), r, names));
            }
        }
        UNPROTECT(1);
        return out;
    }

    SEXP table(const Node& n) {
        const Table& t = d_.tables[n.off];
        SEXP names = PROTECT(Rf_allocVector(VECSXP, static_cast<R_xlen_t>(t.groups.size())));
        for (size_t g = 0; g < t.groups.size(); g++) {
            const auto& keys = t.groups[g].keys;
            SEXP nm = Rf_allocVector(STRSXP, static_cast<R_xlen_t>(keys.size()));
            SET_VECTOR_ELT(names, static_cast<R_xlen_t>(g), nm);
            for (size_t j = 0; j < keys.size(); j++) {
                SET_STRING_ELT(nm, static_cast<R_xlen_t>(j), mkchar_utf8(keys[j]));
            }
        }
        SEXP out = PROTECT(Rf_allocVector(VECSXP, n.len));
        if (t.keyed) {
            SEXP keys = Rf_allocVector(STRSXP, n.len);
            Rf_setAttrib(out, R_NamesSymbol, keys);
            for (uint32_t i = 0; i < n.len; i++) {
                SET_STRING_ELT(keys, i, str(t.keys[i].key_off, t.keys[i].key_len));
                SET_VECTOR_ELT(out, i, row(t, 0, t.rows[i], names));
            }
        } else {
            for (uint32_t i = 0; i < n.len; i++) {
                if ((i & 0xFFFF) == 0xFFFF) check_interrupt();
                SET_VECTOR_ELT(out, i, row(t, 0, i, names));
            }
        }
        UNPROTECT(2);
        return out;
    }

    const Document& d_;
    bool simplify_;
};

void count(const Document& d, uint32_t id, DocumentInfo& info) {
    const Node& n = d.nodes[id];
    switch (n.kind) {
        case NodeKind::Array:
        case NodeKind::Object:
            (n.kind == NodeKind::Array ? info.arrays : info.objects)++;
            for (uint32_t i = 0; i < n.len; i++) count(d, d.kids[n.off + i].node, info);
            break;
        case NodeKind::Table: {
            const Table& t = d.tables[n.off];
            info.objects += n.len * t.groups.size();
            if (t.keyed) {
                info.objects++;
            } else {
                info.arrays++;
                if (!info.has_tabular) {
                    info.has_tabular = true;
                    info.first_table_rows = n.len;
                }
            }
            break;
        }
        default:
            break;
    }
}

}  // namespace

Document parse_toon(const char* data, size_t len, const ReadOptions& opts) {
    BufferedReader src(data, len);
    return Parser(src, opts, "").parse();
}

Document parse_toon_file(const std::string& path, const ReadOptions& opts) {
    BufferedReader src(path);
    if (src.has_error()) throw std::runtime_error(src.error_message());
    return Parser(src, opts, path).parse();
}

SEXP document_to_r(const Document& doc, bool simplify) {
    return RBuilder(doc, simplify).value(doc.root);
}

DocumentInfo document_info(const Document& doc) {
    DocumentInfo info;
    count(doc, doc.root, info);
    return info;
}

PeekInfo peek_toon_file(const std::string& path, size_t max_lines) {
    BufferedReader src(path);
    if (src.has_error()) throw std::runtime_error(src.error_message());
    PeekInfo out;
    std::string_view raw;
    size_t no = 0;
    bool content = false;
    bool keyed_root = false;
    std::string scratch;
    auto add_key = [&](std::string_view tok) {
        if (out.first_keys.size() >= 5) return;
        try {
            decode_key(tok, scratch);
            out.first_keys.push_back(scratch);
        } catch (const LexError&) {
            out.first_keys.emplace_back(tok);
        }
    };
    while (out.preview.size() < max_lines && src.next_line(raw, no)) {
        out.preview.emplace_back(raw);
        std::string_view s = raw;
        if (no == 1 && s.size() >= 3 && std::memcmp(s.data(), "\xEF\xBB\xBF", 3) == 0) {
            s.remove_prefix(3);
        }
        size_t indent = 0;
        bool tab = false;
        while (indent < s.size() && (s[indent] == ' ' || s[indent] == '\t')) {
            tab = tab || s[indent] == '\t';
            indent++;
        }
        s = trim_spaces(s.substr(indent));
        if (s.empty() || (s[0] == '#' && !tab)) continue;
        const LineScan sc = scan_line(s);
        if (!content) {
            content = true;
            if (indent > 0) {
                break;  // not a valid document; leave the type unknown
            } else if (s == "[]") {
                out.type = "array";
            } else if (is_header_line(sc) && sc.bracket == 0) {
                Header h;
                try {
                    parse_header(s, sc, false, h);
                    keyed_root = h.keyed;
                    out.type = h.keyed ? "object" : h.has_fields ? "tabular_array" : "array";
                } catch (const LexError&) {
                }
            } else if (sc.colon == npos) {
                out.type = "primitive";
            } else {
                out.type = "object";
                add_key(trim_spaces(s.substr(0, is_header_line(sc) ? sc.bracket : sc.colon)));
            }
            continue;
        }
        if (out.type == "primitive") {
            out.type = "unknown";  // a second line: not a valid document
        } else if (out.type == "object" && sc.colon != npos) {
            const bool entry = keyed_root && indent > 0;
            if (entry || (!keyed_root && indent == 0)) {
                const bool header = !entry && is_header_line(sc);
                add_key(trim_spaces(s.substr(0, header ? sc.bracket : sc.colon)));
            }
        }
    }
    if (!content && out.preview.size() < max_lines) out.type = "object";  // empty document
    return out;
}

}  // namespace toonlite
