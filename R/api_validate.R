#' @title TOON Validation and Formatting
#' @name validation
#' @description Functions for validating and formatting TOON data.
NULL

#' Validate TOON
#'
#' Checks a document against the TOON spec (v4.4) by decoding it.
#'
#' @param x Character scalar containing TOON text, or file path if is_file=TRUE.
#' @param is_file Logical. If TRUE, x is treated as a file path.
#' @inheritParams from_toon
#'
#' @return Logical scalar.
#'   \itemize{
#'     \item TRUE if valid.
#'     \item FALSE if invalid, with attribute \code{attr(result, "error")}
#'       containing a structured error object with components: type, message,
#'       line, column, snippet, file.
#'   }
#'
#' @details
#' Never throws unless there's an internal error (e.g., file unreadable).
#'
#' @examples
#' validate_toon("key: value")
#'
#' # Invalid TOON (returns FALSE with error attribute)
#' result <- validate_toon("items[3]: a,b")
#' if (!result) print(attr(result, "error")$message)
#'
#' @export
validate_toon <- function(x, is_file = FALSE, strict = TRUE,
                          allow_comments = TRUE, allow_duplicate_keys = TRUE,
                          indent = 2L) {
  check_string(x)
  warn_comments_deprecated(allow_comments)
  if (!missing(allow_duplicate_keys)) warn_decoder_duplicate_keys_deprecated()
  indent <- check_indent(indent)

  if (is_file) {
    if (!file.exists(x)) {
      # Return FALSE with error attribute for missing file
      result <- FALSE
      attr(result, "error") <- list(
        type = "io_error",
        message = paste("File not found:", x),
        line = NA_integer_,
        column = NA_integer_,
        snippet = NA_character_,
        file = x
      )
      return(result)
    }
    x <- normalizePath(x)
  }

  .Call(C_validate_toon, x, isTRUE(is_file), isTRUE(strict), indent)
}

#' Assert TOON validity
#'
#' @inheritParams validate_toon
#'
#' @return Invisibly returns TRUE if valid.
#'
#' @details
#' Throws \code{toonlite_parse_error} condition if invalid, with attached
#' location and snippet information.
#'
#' @examples
#' # Valid TOON (returns invisibly)
#' assert_toon("name: Alice")
#'
#' # Invalid TOON (throws error)
#' try(assert_toon("items[2]: a"))
#'
#' @export
assert_toon <- function(x, is_file = FALSE, strict = TRUE,
                        allow_comments = TRUE, allow_duplicate_keys = TRUE,
                        indent = 2L) {
  warn_comments_deprecated(allow_comments)
  if (!missing(allow_duplicate_keys)) warn_decoder_duplicate_keys_deprecated()
  result <- validate_toon(x, is_file = is_file, strict = strict, indent = indent)

  if (!result) {
    error_info <- attr(result, "error")
    stop_toonlite_parse(
      message = error_info$message,
      line = error_info$line,
      column = error_info$column,
      snippet = error_info$snippet,
      file = error_info$file
    )
  }

  invisible(TRUE)
}

#' Format/pretty-print TOON
#'
#' Decodes TOON and encodes it again with [to_toon()], which gives the
#' canonical spec form: normalised quoting and numbers, inline primitive
#' arrays, tabular forms where they apply.
#'
#' @param x Character scalar containing TOON text, or file path if is_file=TRUE.
#' @param is_file Logical. If TRUE, x is treated as a file path.
#' @param indent Integer. Spaces per indentation level in the output
#'   (default 2).
#' @param canonical Logical. If TRUE, sort object keys (byte order, at every
#'   level) for a stable representation. Default FALSE (preserve order).
#' @param allow_comments Deprecated and ignored. Full-line `#` comments are
#'   always skipped (and never written).
#' @param delimiter Document delimiter for the output: `","` (default),
#'   `"\t"` or `"|"`.
#'
#' @return Character scalar with formatted TOON. If is_file=TRUE, returns
#'   formatted text (does not rewrite file).
#'
#' @examples
#' format_toon('name: "Alice"')
#'
#' # Format with canonical key ordering
#' format_toon('b: 1\na: 2', canonical = TRUE)
#'
#' @export
format_toon <- function(x, is_file = FALSE, indent = 2L, canonical = FALSE,
                        allow_comments = TRUE, delimiter = ",") {
  check_string(x)
  warn_comments_deprecated(allow_comments)
  indent <- check_indent(indent)
  delimiter <- check_delimiter(delimiter)

  value <- if (is_file) {
    read_toon(x, simplify = FALSE)
  } else {
    from_toon(x, simplify = FALSE)
  }
  if (isTRUE(canonical)) value <- sort_keys(value)

  unclass(.Call(C_to_toon, value, indent, delimiter))
}

# Sort the keys of every object (named list) in byte order.
sort_keys <- function(x) {
  if (!is.list(x) || is.data.frame(x)) return(x)
  if (!is.null(names(x))) x <- x[order(names(x), method = "radix")]
  x[] <- lapply(x, sort_keys)
  x
}

#' Peek at TOON file structure
#'
#' Looks at the first `n` lines only, without decoding or validating the
#' document.
#'
#' @param file Character scalar. Path to TOON file.
#' @param n Integer. Number of lines to preview (default 50).
#' @param allow_comments Deprecated and ignored. Comment lines are always
#'   skipped; passing anything other than TRUE warns.
#'
#' @return A list with components:
#'   \itemize{
#'     \item type: Character. The root form: "object", "array",
#'       "tabular_array", "primitive" or "unknown" (no content in the first
#'       `n` lines, or not a valid root).
#'     \item first_keys: Character vector. Up to five keys of a root object
#'       (entry keys for a keyed table).
#'     \item preview: Character vector. The first n lines as written.
#'   }
#'
#' @examples
#' f <- tempfile(fileext = ".toon")
#' write_toon(list(name = "Ada", tags = c("x", "y")), f)
#' toon_peek(f)
#'
#' @export
toon_peek <- function(file, n = 50L, allow_comments = TRUE) {
  check_string(file)
  file <- normalizePath(file, mustWork = TRUE)
  warn_comments_deprecated(allow_comments)

  n <- as.integer(n)
  if (is.na(n) || n < 1) n <- 1L

  .Call(C_toon_peek, file, n)
}

#' Get TOON file info
#'
#' Decodes the file and counts its arrays and objects.
#'
#' @param file Character scalar. Path to TOON file.
#' @param allow_comments Deprecated and ignored. Comment lines are always
#'   skipped; passing anything other than TRUE warns.
#' @inheritParams from_toon
#'
#' @return A list with components:
#'   \itemize{
#'     \item array_count: Integer. Number of arrays in the document.
#'     \item object_count: Integer. Number of objects in the document
#'       (including the rows of tabular arrays).
#'     \item has_tabular: Logical. Whether the document contains a tabular
#'       array.
#'     \item declared_rows: Integer or NA. Rows of the first tabular array.
#'   }
#'
#' @examples
#' f <- tempfile(fileext = ".toon")
#' write_toon(list(users = data.frame(id = 1:3)), f)
#' toon_info(f)
#'
#' @export
toon_info <- function(file, allow_comments = TRUE, strict = TRUE, indent = 2L) {
  check_string(file)
  file <- normalizePath(file, mustWork = TRUE)
  warn_comments_deprecated(allow_comments)
  indent <- check_indent(indent)

  .Call(C_toon_info, file, isTRUE(strict), indent)
}
