#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>

#include "toon_parser.hpp"
#include "toon_encoder.hpp"
#include "toon_df.hpp"
#include "toon_stream.hpp"
#include "toon_errors.hpp"

using namespace toonlite;

// Helper to convert Node to SEXP
static SEXP node_to_sexp(const NodePtr& node, bool simplify) {
    if (!node) return R_NilValue;

    switch (node->kind) {
        case NodeKind::N_NULL:
            return R_NilValue;

        case NodeKind::N_BOOL: {
            SEXP result = PROTECT(Rf_allocVector(LGLSXP, 1));
            LOGICAL(result)[0] = node->bool_val ? TRUE : FALSE;
            UNPROTECT(1);
            return result;
        }

        case NodeKind::N_INT: {
            SEXP result = PROTECT(Rf_allocVector(INTSXP, 1));
            INTEGER(result)[0] = static_cast<int>(node->int_val);
            UNPROTECT(1);
            return result;
        }

        case NodeKind::N_DOUBLE: {
            SEXP result = PROTECT(Rf_allocVector(REALSXP, 1));
            REAL(result)[0] = node->double_val;
            UNPROTECT(1);
            return result;
        }

        case NodeKind::N_STRING: {
            SEXP result = PROTECT(Rf_allocVector(STRSXP, 1));
            SET_STRING_ELT(result, 0, Rf_mkCharCE(node->string_val.c_str(), CE_UTF8));
            UNPROTECT(1);
            return result;
        }

        case NodeKind::N_ARRAY: {
            size_t n = node->array_items.size();

            if (simplify && n > 0) {
                // Check if all items are same primitive type
                NodeKind first_kind = node->array_items[0]->kind;
                bool all_same = true;
                bool all_primitive = true;

                for (const auto& item : node->array_items) {
                    if (item->kind == NodeKind::N_ARRAY || item->kind == NodeKind::N_OBJECT) {
                        all_primitive = false;
                        break;
                    }
                    if (item->kind != first_kind && item->kind != NodeKind::N_NULL) {
                        if (first_kind == NodeKind::N_NULL) {
                            first_kind = item->kind;
                        } else {
                            all_same = false;
                        }
                    }
                }

                if (all_primitive && all_same) {
                    // Convert to atomic vector
                    switch (first_kind) {
                        case NodeKind::N_BOOL: {
                            SEXP result = PROTECT(Rf_allocVector(LGLSXP, n));
                            int* data = LOGICAL(result);
                            for (size_t i = 0; i < n; i++) {
                                if (node->array_items[i]->kind == NodeKind::N_NULL) {
                                    data[i] = NA_LOGICAL;
                                } else {
                                    data[i] = node->array_items[i]->bool_val ? TRUE : FALSE;
                                }
                            }
                            UNPROTECT(1);
                            return result;
                        }
                        case NodeKind::N_INT: {
                            SEXP result = PROTECT(Rf_allocVector(INTSXP, n));
                            int* data = INTEGER(result);
                            for (size_t i = 0; i < n; i++) {
                                if (node->array_items[i]->kind == NodeKind::N_NULL) {
                                    data[i] = NA_INTEGER;
                                } else {
                                    data[i] = static_cast<int>(node->array_items[i]->int_val);
                                }
                            }
                            UNPROTECT(1);
                            return result;
                        }
                        case NodeKind::N_DOUBLE: {
                            SEXP result = PROTECT(Rf_allocVector(REALSXP, n));
                            double* data = REAL(result);
                            for (size_t i = 0; i < n; i++) {
                                if (node->array_items[i]->kind == NodeKind::N_NULL) {
                                    data[i] = NA_REAL;
                                } else {
                                    data[i] = node->array_items[i]->double_val;
                                }
                            }
                            UNPROTECT(1);
                            return result;
                        }
                        case NodeKind::N_STRING: {
                            SEXP result = PROTECT(Rf_allocVector(STRSXP, n));
                            for (size_t i = 0; i < n; i++) {
                                if (node->array_items[i]->kind == NodeKind::N_NULL) {
                                    SET_STRING_ELT(result, i, NA_STRING);
                                } else {
                                    SET_STRING_ELT(result, i,
                                        Rf_mkCharCE(node->array_items[i]->string_val.c_str(), CE_UTF8));
                                }
                            }
                            UNPROTECT(1);
                            return result;
                        }
                        default:
                            break;
                    }
                }
            }

            // Return as list
            SEXP result = PROTECT(Rf_allocVector(VECSXP, n));
            for (size_t i = 0; i < n; i++) {
                SET_VECTOR_ELT(result, i, node_to_sexp(node->array_items[i], simplify));
            }
            UNPROTECT(1);
            return result;
        }

        case NodeKind::N_OBJECT: {
            size_t n = node->object_items.size();
            SEXP result = PROTECT(Rf_allocVector(VECSXP, n));
            SEXP names = PROTECT(Rf_allocVector(STRSXP, n));

            for (size_t i = 0; i < n; i++) {
                SET_STRING_ELT(names, i, Rf_mkCharCE(node->object_items[i].first.c_str(), CE_UTF8));
                SET_VECTOR_ELT(result, i, node_to_sexp(node->object_items[i].second, simplify));
            }

            Rf_setAttrib(result, R_NamesSymbol, names);
            UNPROTECT(2);
            return result;
        }
    }

    return R_NilValue;
}

// Helper to emit warnings from parser
static void emit_warnings(const std::vector<Warning>& warnings) {
    for (const auto& w : warnings) {
        Rf_warning("%s", w.message.c_str());
    }
}

extern "C" {

// Parse TOON string to R object
SEXP C_from_toon(SEXP text, SEXP strict, SEXP simplify, SEXP allow_comments, SEXP allow_duplicate_keys) {
    try {
        ParseOptions opts;
        opts.strict = Rf_asLogical(strict) == TRUE;
        opts.simplify = Rf_asLogical(simplify) == TRUE;
        opts.allow_comments = Rf_asLogical(allow_comments) == TRUE;
        opts.allow_duplicate_keys = Rf_asLogical(allow_duplicate_keys) == TRUE;

        Parser parser(opts);

        const char* data;
        size_t len;

        if (TYPEOF(text) == RAWSXP) {
            data = reinterpret_cast<const char*>(RAW(text));
            len = Rf_xlength(text);
        } else {
            data = CHAR(STRING_ELT(text, 0));
            len = strlen(data);
        }

        NodePtr node = parser.parse_string(data, len);
        emit_warnings(parser.warnings());

        return node_to_sexp(node, opts.simplify);
    } catch (const ParseError& e) {
        Rf_error("%s", e.formatted_message().c_str());
    } catch (const std::exception& e) {
        Rf_error("Error parsing TOON: %s", e.what());
    }

    return R_NilValue;
}

// Read TOON file to R object
SEXP C_read_toon(SEXP file, SEXP strict, SEXP simplify, SEXP allow_comments, SEXP allow_duplicate_keys) {
    try {
        ParseOptions opts;
        opts.strict = Rf_asLogical(strict) == TRUE;
        opts.simplify = Rf_asLogical(simplify) == TRUE;
        opts.allow_comments = Rf_asLogical(allow_comments) == TRUE;
        opts.allow_duplicate_keys = Rf_asLogical(allow_duplicate_keys) == TRUE;

        Parser parser(opts);
        std::string filepath(CHAR(STRING_ELT(file, 0)));

        NodePtr node = parser.parse_file(filepath);
        emit_warnings(parser.warnings());

        return node_to_sexp(node, opts.simplify);
    } catch (const ParseError& e) {
        Rf_error("%s", e.formatted_message().c_str());
    } catch (const std::exception& e) {
        Rf_error("Error reading TOON file: %s", e.what());
    }

    return R_NilValue;
}

// Encode R object to TOON string
SEXP C_to_toon(SEXP x, SEXP pretty, SEXP indent, SEXP strict) {
    try {
        EncodeOptions opts;
        opts.pretty = Rf_asLogical(pretty) == TRUE;
        opts.indent = Rf_asInteger(indent);
        opts.strict = Rf_asLogical(strict) == TRUE;

        Encoder encoder(opts);
        std::string result = encoder.encode(x);

        SEXP out = PROTECT(Rf_allocVector(STRSXP, 1));
        SET_STRING_ELT(out, 0, Rf_mkCharCE(result.c_str(), CE_UTF8));

        // Set class
        SEXP class_attr = PROTECT(Rf_allocVector(STRSXP, 1));
        SET_STRING_ELT(class_attr, 0, Rf_mkChar("toon"));
        Rf_setAttrib(out, R_ClassSymbol, class_attr);

        UNPROTECT(2);
        return out;
    } catch (const ParseError& e) {
        Rf_error("%s", e.formatted_message().c_str());
    } catch (const std::exception& e) {
        Rf_error("Error encoding to TOON: %s", e.what());
    }

    return R_NilValue;
}

// Validate TOON text or file
SEXP C_validate_toon(SEXP x, SEXP is_file, SEXP strict, SEXP allow_comments, SEXP allow_duplicate_keys) {
    ParseOptions opts;
    opts.strict = Rf_asLogical(strict) == TRUE;
    opts.allow_comments = Rf_asLogical(allow_comments) == TRUE;
    opts.allow_duplicate_keys = Rf_asLogical(allow_duplicate_keys) == TRUE;

    Parser parser(opts);
    ValidationResult vr;

    if (Rf_asLogical(is_file) == TRUE) {
        std::string filepath(CHAR(STRING_ELT(x, 0)));
        vr = parser.validate_file(filepath);
    } else {
        const char* text = CHAR(STRING_ELT(x, 0));
        vr = parser.validate_string(text);
    }

    // Create result
    SEXP result = PROTECT(Rf_allocVector(LGLSXP, 1));
    LOGICAL(result)[0] = vr.valid ? TRUE : FALSE;

    if (!vr.valid) {
        // Attach error info
        SEXP error_info = PROTECT(Rf_allocVector(VECSXP, 6));
        SEXP error_names = PROTECT(Rf_allocVector(STRSXP, 6));

        SET_STRING_ELT(error_names, 0, Rf_mkChar("type"));
        SET_STRING_ELT(error_names, 1, Rf_mkChar("message"));
        SET_STRING_ELT(error_names, 2, Rf_mkChar("line"));
        SET_STRING_ELT(error_names, 3, Rf_mkChar("column"));
        SET_STRING_ELT(error_names, 4, Rf_mkChar("snippet"));
        SET_STRING_ELT(error_names, 5, Rf_mkChar("file"));

        SEXP type_str = PROTECT(Rf_allocVector(STRSXP, 1));
        SET_STRING_ELT(type_str, 0, Rf_mkChar("parse_error"));
        SET_VECTOR_ELT(error_info, 0, type_str);

        SEXP msg_str = PROTECT(Rf_allocVector(STRSXP, 1));
        SET_STRING_ELT(msg_str, 0, Rf_mkCharCE(vr.message.c_str(), CE_UTF8));
        SET_VECTOR_ELT(error_info, 1, msg_str);

        SEXP line_int = PROTECT(Rf_allocVector(INTSXP, 1));
        INTEGER(line_int)[0] = vr.line > 0 ? static_cast<int>(vr.line) : NA_INTEGER;
        SET_VECTOR_ELT(error_info, 2, line_int);

        SEXP col_int = PROTECT(Rf_allocVector(INTSXP, 1));
        INTEGER(col_int)[0] = vr.column > 0 ? static_cast<int>(vr.column) : NA_INTEGER;
        SET_VECTOR_ELT(error_info, 3, col_int);

        SEXP snip_str = PROTECT(Rf_allocVector(STRSXP, 1));
        SET_STRING_ELT(snip_str, 0, vr.snippet.empty() ? NA_STRING : Rf_mkCharCE(vr.snippet.c_str(), CE_UTF8));
        SET_VECTOR_ELT(error_info, 4, snip_str);

        SEXP file_str = PROTECT(Rf_allocVector(STRSXP, 1));
        SET_STRING_ELT(file_str, 0, vr.file.empty() ? NA_STRING : Rf_mkCharCE(vr.file.c_str(), CE_UTF8));
        SET_VECTOR_ELT(error_info, 5, file_str);

        Rf_setAttrib(error_info, R_NamesSymbol, error_names);
        Rf_setAttrib(result, Rf_install("error"), error_info);

        UNPROTECT(9);
    } else {
        UNPROTECT(1);
    }

    return result;
}

// Read tabular TOON to data.frame
SEXP C_read_toon_df(SEXP file, SEXP key, SEXP strict, SEXP allow_comments,
                    SEXP allow_duplicate_keys, SEXP warn, SEXP col_types,
                    SEXP ragged_rows, SEXP n_mismatch, SEXP max_extra_cols) {
    try {
        TabularParseOptions opts;
        opts.strict = Rf_asLogical(strict) == TRUE;
        opts.allow_comments = Rf_asLogical(allow_comments) == TRUE;
        opts.allow_duplicate_keys = Rf_asLogical(allow_duplicate_keys) == TRUE;
        opts.warn = Rf_asLogical(warn) == TRUE;

        if (key != R_NilValue) {
            opts.key = std::string(CHAR(STRING_ELT(key, 0)));
        }

        opts.ragged_rows = CHAR(STRING_ELT(ragged_rows, 0));
        opts.n_mismatch = CHAR(STRING_ELT(n_mismatch, 0));

        double max_cols = Rf_asReal(max_extra_cols);
        opts.max_extra_cols = std::isinf(max_cols) ? SIZE_MAX : static_cast<size_t>(max_cols);

        // Parse col_types if provided
        if (col_types != R_NilValue && Rf_xlength(col_types) > 0) {
            SEXP ct_names = Rf_getAttrib(col_types, R_NamesSymbol);
            for (R_xlen_t i = 0; i < Rf_xlength(col_types); i++) {
                std::string name(CHAR(STRING_ELT(ct_names, i)));
                std::string type_str(CHAR(STRING_ELT(col_types, i)));

                ColType ctype = ColType::STRING;
                if (type_str == "logical") ctype = ColType::LOGICAL;
                else if (type_str == "integer") ctype = ColType::INTEGER;
                else if (type_str == "double") ctype = ColType::DOUBLE;
                else if (type_str == "character") ctype = ColType::STRING;

                opts.col_types.push_back({name, ctype});
            }
        }

        TabularParser parser(opts);
        std::string filepath(CHAR(STRING_ELT(file, 0)));

        SEXP result = parser.parse_file(filepath);
        emit_warnings(parser.warnings());

        return result;
    } catch (const ParseError& e) {
        Rf_error("%s", e.formatted_message().c_str());
    } catch (const std::exception& e) {
        Rf_error("Error reading tabular TOON: %s", e.what());
    }

    return R_NilValue;
}

// Write data.frame to tabular TOON
SEXP C_write_toon_df(SEXP df, SEXP file, SEXP tabular, SEXP pretty, SEXP indent, SEXP strict) {
    try {
        EncodeOptions opts;
        opts.pretty = Rf_asLogical(pretty) == TRUE;
        opts.indent = Rf_asInteger(indent);
        opts.strict = Rf_asLogical(strict) == TRUE;

        Encoder encoder(opts);
        std::string result = encoder.encode_dataframe(df, Rf_asLogical(tabular) == TRUE);

        // Write to file
        std::string filepath(CHAR(STRING_ELT(file, 0)));
        std::ofstream out(filepath, std::ios::binary);
        if (!out.is_open()) {
            Rf_error("Cannot open file for writing: %s", filepath.c_str());
        }
        out << result;
        out.close();

        return R_NilValue;
    } catch (const ParseError& e) {
        Rf_error("%s", e.formatted_message().c_str());
    } catch (const std::exception& e) {
        Rf_error("Error writing tabular TOON: %s", e.what());
    }

    return R_NilValue;
}

// Stream tabular rows
SEXP C_stream_rows(SEXP file, SEXP key, SEXP callback, SEXP batch_size,
                   SEXP strict, SEXP allow_comments, SEXP allow_duplicate_keys,
                   SEXP warn, SEXP col_types, SEXP ragged_rows, SEXP n_mismatch,
                   SEXP max_extra_cols) {
    try {
        StreamOptions opts;
        opts.strict = Rf_asLogical(strict) == TRUE;
        opts.allow_comments = Rf_asLogical(allow_comments) == TRUE;
        opts.allow_duplicate_keys = Rf_asLogical(allow_duplicate_keys) == TRUE;
        opts.warn = Rf_asLogical(warn) == TRUE;
        opts.batch_size = static_cast<size_t>(Rf_asInteger(batch_size));

        if (key != R_NilValue) {
            opts.key = std::string(CHAR(STRING_ELT(key, 0)));
        }

        opts.ragged_rows = CHAR(STRING_ELT(ragged_rows, 0));
        opts.n_mismatch = CHAR(STRING_ELT(n_mismatch, 0));

        double max_cols = Rf_asReal(max_extra_cols);
        opts.max_extra_cols = std::isinf(max_cols) ? SIZE_MAX : static_cast<size_t>(max_cols);

        // Parse col_types
        if (col_types != R_NilValue && Rf_xlength(col_types) > 0) {
            SEXP ct_names = Rf_getAttrib(col_types, R_NamesSymbol);
            for (R_xlen_t i = 0; i < Rf_xlength(col_types); i++) {
                std::string name(CHAR(STRING_ELT(ct_names, i)));
                std::string type_str(CHAR(STRING_ELT(col_types, i)));

                ColType ctype = ColType::STRING;
                if (type_str == "logical") ctype = ColType::LOGICAL;
                else if (type_str == "integer") ctype = ColType::INTEGER;
                else if (type_str == "double") ctype = ColType::DOUBLE;
                else if (type_str == "character") ctype = ColType::STRING;

                opts.col_types.push_back({name, ctype});
            }
        }

        std::string filepath(CHAR(STRING_ELT(file, 0)));
        RowStreamer streamer(filepath, opts);
        streamer.stream(callback);
        emit_warnings(streamer.warnings());

        return R_NilValue;
    } catch (const ParseError& e) {
        Rf_error("%s", e.formatted_message().c_str());
    } catch (const std::exception& e) {
        Rf_error("Error streaming TOON: %s", e.what());
    }

    return R_NilValue;
}

// Format/pretty-print TOON
SEXP C_format_toon(SEXP x, SEXP is_file, SEXP indent, SEXP canonical, SEXP allow_comments) {
    try {
        ParseOptions parse_opts;
        parse_opts.allow_comments = Rf_asLogical(allow_comments) == TRUE;
        parse_opts.simplify = false;

        Parser parser(parse_opts);
        NodePtr node;

        if (Rf_asLogical(is_file) == TRUE) {
            std::string filepath(CHAR(STRING_ELT(x, 0)));
            node = parser.parse_file(filepath);
        } else {
            const char* text = CHAR(STRING_ELT(x, 0));
            node = parser.parse_string(text);
        }

        EncodeOptions enc_opts;
        enc_opts.pretty = true;
        enc_opts.indent = Rf_asInteger(indent);
        enc_opts.canonical = Rf_asLogical(canonical) == TRUE;

        Encoder encoder(enc_opts);
        SEXP r_node = node_to_sexp(node, false);
        std::string result = encoder.encode(r_node);

        SEXP out = PROTECT(Rf_allocVector(STRSXP, 1));
        SET_STRING_ELT(out, 0, Rf_mkCharCE(result.c_str(), CE_UTF8));
        UNPROTECT(1);
        return out;
    } catch (const ParseError& e) {
        Rf_error("%s", e.formatted_message().c_str());
    } catch (const std::exception& e) {
        Rf_error("Error formatting TOON: %s", e.what());
    }

    return R_NilValue;
}

// Peek at TOON file structure
SEXP C_toon_peek(SEXP file, SEXP n, SEXP allow_comments) {
    try {
        std::string filepath(CHAR(STRING_ELT(file, 0)));
        int max_lines = Rf_asInteger(n);

        BufferedReader reader(filepath);
        if (reader.has_error()) {
            Rf_error("%s", reader.error_message().c_str());
        }

        std::vector<std::string> lines;
        std::string_view line;
        size_t line_no;
        std::string top_type = "unknown";
        std::vector<std::string> first_keys;

        while (reader.next_line(line, line_no) && static_cast<int>(lines.size()) < max_lines) {
            lines.push_back(std::string(line));

            // Try to determine top-level type from first non-empty line
            if (top_type == "unknown" && !line.empty()) {
                size_t indent = 0;
                while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
                    indent++;
                }
                if (indent < line.size()) {
                    char c = line[indent];
                    if (c == '[') {
                        if (line.find('{') != std::string::npos) {
                            top_type = "tabular_array";
                        } else {
                            top_type = "array";
                        }
                    } else if (c == '-') {
                        top_type = "array";
                    } else if (c != '#' && c != '/') {
                        // Look for colon
                        size_t colon = line.find(':');
                        if (colon != std::string::npos) {
                            top_type = "object";
                            std::string key(line.substr(indent, colon - indent));
                            // Trim key
                            while (!key.empty() && std::isspace(key.back())) key.pop_back();
                            if (!key.empty() && first_keys.size() < 5) {
                                first_keys.push_back(key);
                            }
                        }
                    }
                }
            } else if (top_type == "object" && first_keys.size() < 5) {
                // Collect more keys
                size_t indent = 0;
                while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
                    indent++;
                }
                if (indent == 0) {  // Top-level key
                    size_t colon = line.find(':');
                    if (colon != std::string::npos) {
                        std::string key(line.substr(0, colon));
                        while (!key.empty() && std::isspace(key.back())) key.pop_back();
                        if (!key.empty()) {
                            first_keys.push_back(key);
                        }
                    }
                }
            }
        }

        // Build result list
        SEXP result = PROTECT(Rf_allocVector(VECSXP, 3));
        SEXP result_names = PROTECT(Rf_allocVector(STRSXP, 3));

        SET_STRING_ELT(result_names, 0, Rf_mkChar("type"));
        SET_STRING_ELT(result_names, 1, Rf_mkChar("first_keys"));
        SET_STRING_ELT(result_names, 2, Rf_mkChar("preview"));

        // Type
        SEXP type_str = PROTECT(Rf_allocVector(STRSXP, 1));
        SET_STRING_ELT(type_str, 0, Rf_mkChar(top_type.c_str()));
        SET_VECTOR_ELT(result, 0, type_str);

        // First keys
        SEXP keys_vec = PROTECT(Rf_allocVector(STRSXP, first_keys.size()));
        for (size_t i = 0; i < first_keys.size(); i++) {
            SET_STRING_ELT(keys_vec, i, Rf_mkCharCE(first_keys[i].c_str(), CE_UTF8));
        }
        SET_VECTOR_ELT(result, 1, keys_vec);

        // Preview lines
        SEXP preview_vec = PROTECT(Rf_allocVector(STRSXP, lines.size()));
        for (size_t i = 0; i < lines.size(); i++) {
            SET_STRING_ELT(preview_vec, i, Rf_mkCharCE(lines[i].c_str(), CE_UTF8));
        }
        SET_VECTOR_ELT(result, 2, preview_vec);

        Rf_setAttrib(result, R_NamesSymbol, result_names);

        UNPROTECT(5);
        return result;
    } catch (const std::exception& e) {
        Rf_error("Error peeking TOON: %s", e.what());
    }

    return R_NilValue;
}

// Get TOON file info
SEXP C_toon_info(SEXP file, SEXP allow_comments) {
    try {
        ParseOptions opts;
        opts.allow_comments = Rf_asLogical(allow_comments) == TRUE;
        opts.simplify = false;

        std::string filepath(CHAR(STRING_ELT(file, 0)));

        Parser parser(opts);
        NodePtr node = parser.parse_file(filepath);

        // Count arrays and objects recursively
        std::function<void(const NodePtr&, int&, int&, bool&, size_t&)> count_nodes;
        count_nodes = [&count_nodes](const NodePtr& n, int& arrays, int& objects, bool& has_tabular, size_t& declared_rows) {
            if (!n) return;

            if (n->kind == NodeKind::N_ARRAY) {
                arrays++;
                // Check if it's a tabular array (all items are objects with same keys)
                if (!n->array_items.empty()) {
                    bool all_objects = true;
                    for (const auto& item : n->array_items) {
                        if (item->kind != NodeKind::N_OBJECT) {
                            all_objects = false;
                            break;
                        }
                    }
                    if (all_objects) {
                        has_tabular = true;
                        declared_rows = n->array_items.size();
                    }
                }
                for (const auto& item : n->array_items) {
                    count_nodes(item, arrays, objects, has_tabular, declared_rows);
                }
            } else if (n->kind == NodeKind::N_OBJECT) {
                objects++;
                for (const auto& item : n->object_items) {
                    count_nodes(item.second, arrays, objects, has_tabular, declared_rows);
                }
            }
        };

        int array_count = 0, object_count = 0;
        bool has_tabular = false;
        size_t declared_rows = 0;
        count_nodes(node, array_count, object_count, has_tabular, declared_rows);

        // Build result
        SEXP result = PROTECT(Rf_allocVector(VECSXP, 4));
        SEXP result_names = PROTECT(Rf_allocVector(STRSXP, 4));

        SET_STRING_ELT(result_names, 0, Rf_mkChar("array_count"));
        SET_STRING_ELT(result_names, 1, Rf_mkChar("object_count"));
        SET_STRING_ELT(result_names, 2, Rf_mkChar("has_tabular"));
        SET_STRING_ELT(result_names, 3, Rf_mkChar("declared_rows"));

        SEXP arr_int = PROTECT(Rf_ScalarInteger(array_count));
        SEXP obj_int = PROTECT(Rf_ScalarInteger(object_count));
        SEXP tab_lgl = PROTECT(Rf_ScalarLogical(has_tabular ? TRUE : FALSE));
        SEXP rows_int = PROTECT(Rf_ScalarInteger(has_tabular ? static_cast<int>(declared_rows) : NA_INTEGER));

        SET_VECTOR_ELT(result, 0, arr_int);
        SET_VECTOR_ELT(result, 1, obj_int);
        SET_VECTOR_ELT(result, 2, tab_lgl);
        SET_VECTOR_ELT(result, 3, rows_int);

        Rf_setAttrib(result, R_NamesSymbol, result_names);

        UNPROTECT(6);
        return result;
    } catch (const ParseError& e) {
        Rf_error("%s", e.formatted_message().c_str());
    } catch (const std::exception& e) {
        Rf_error("Error getting TOON info: %s", e.what());
    }

    return R_NilValue;
}

// From tabular TOON string
SEXP C_from_toon_df(SEXP text, SEXP key, SEXP strict, SEXP allow_comments,
                    SEXP allow_duplicate_keys, SEXP warn, SEXP col_types,
                    SEXP ragged_rows, SEXP n_mismatch, SEXP max_extra_cols) {
    try {
        TabularParseOptions opts;
        opts.strict = Rf_asLogical(strict) == TRUE;
        opts.allow_comments = Rf_asLogical(allow_comments) == TRUE;
        opts.allow_duplicate_keys = Rf_asLogical(allow_duplicate_keys) == TRUE;
        opts.warn = Rf_asLogical(warn) == TRUE;

        if (key != R_NilValue) {
            opts.key = std::string(CHAR(STRING_ELT(key, 0)));
        }

        opts.ragged_rows = CHAR(STRING_ELT(ragged_rows, 0));
        opts.n_mismatch = CHAR(STRING_ELT(n_mismatch, 0));

        double max_cols = Rf_asReal(max_extra_cols);
        opts.max_extra_cols = std::isinf(max_cols) ? SIZE_MAX : static_cast<size_t>(max_cols);

        // Parse col_types
        if (col_types != R_NilValue && Rf_xlength(col_types) > 0) {
            SEXP ct_names = Rf_getAttrib(col_types, R_NamesSymbol);
            for (R_xlen_t i = 0; i < Rf_xlength(col_types); i++) {
                std::string name(CHAR(STRING_ELT(ct_names, i)));
                std::string type_str(CHAR(STRING_ELT(col_types, i)));

                ColType ctype = ColType::STRING;
                if (type_str == "logical") ctype = ColType::LOGICAL;
                else if (type_str == "integer") ctype = ColType::INTEGER;
                else if (type_str == "double") ctype = ColType::DOUBLE;
                else if (type_str == "character") ctype = ColType::STRING;

                opts.col_types.push_back({name, ctype});
            }
        }

        const char* data;
        size_t len;

        if (TYPEOF(text) == RAWSXP) {
            data = reinterpret_cast<const char*>(RAW(text));
            len = Rf_xlength(text);
        } else {
            data = CHAR(STRING_ELT(text, 0));
            len = strlen(data);
        }

        TabularParser parser(opts);
        SEXP result = parser.parse_string(data, len);
        emit_warnings(parser.warnings());

        return result;
    } catch (const ParseError& e) {
        Rf_error("%s", e.formatted_message().c_str());
    } catch (const std::exception& e) {
        Rf_error("Error parsing tabular TOON: %s", e.what());
    }

    return R_NilValue;
}

// Stream write rows
SEXP C_stream_write_init(SEXP file, SEXP schema, SEXP indent) {
    try {
        std::string filepath(CHAR(STRING_ELT(file, 0)));
        std::vector<std::string> schema_vec;

        for (R_xlen_t i = 0; i < Rf_xlength(schema); i++) {
            schema_vec.push_back(CHAR(STRING_ELT(schema, i)));
        }

        auto* writer = new StreamWriter(filepath, schema_vec, Rf_asInteger(indent));

        SEXP ptr = PROTECT(R_MakeExternalPtr(writer, R_NilValue, R_NilValue));
        R_RegisterCFinalizerEx(ptr, [](SEXP p) {
            auto* w = static_cast<StreamWriter*>(R_ExternalPtrAddr(p));
            if (w) {
                delete w;
                R_ClearExternalPtr(p);
            }
        }, TRUE);

        UNPROTECT(1);
        return ptr;
    } catch (const std::exception& e) {
        Rf_error("Error initializing stream writer: %s", e.what());
    }

    return R_NilValue;
}

SEXP C_stream_write_batch(SEXP ptr, SEXP df_batch) {
    try {
        auto* writer = static_cast<StreamWriter*>(R_ExternalPtrAddr(ptr));
        if (!writer) {
            Rf_error("Stream writer has been closed");
        }
        writer->write_batch(df_batch);
        return R_NilValue;
    } catch (const std::exception& e) {
        Rf_error("Error writing batch: %s", e.what());
    }

    return R_NilValue;
}

SEXP C_stream_write_close(SEXP ptr) {
    try {
        auto* writer = static_cast<StreamWriter*>(R_ExternalPtrAddr(ptr));
        if (writer) {
            writer->close();
            delete writer;
            R_ClearExternalPtr(ptr);
        }
        return R_NilValue;
    } catch (const std::exception& e) {
        Rf_error("Error closing stream writer: %s", e.what());
    }

    return R_NilValue;
}

} // extern "C"
