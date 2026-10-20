#' @title Core TOON I/O functions
#' @name core-io
#' @description Functions for parsing and serializing TOON data.
NULL

#' Parse TOON from string
#'
#' @param text Character scalar or raw vector containing TOON data. If raw,
#'   treated as UTF-8 bytes.
#' @param strict Logical. If TRUE (default), enforce strict TOON syntax.
#' @param simplify Logical. If TRUE (default), simplify homogeneous arrays to
#'   atomic vectors.
#' @param allow_comments Logical. If TRUE (default), allow # and // comments.
#' @param allow_duplicate_keys Logical. If TRUE (default), allow duplicate keys
#'   in objects (last-one-wins semantics).
#'
#' @return R object representing the parsed TOON data:
#'   \itemize{
#'     \item object -> named list
#'     \item array -> list or atomic vector (if simplified)
#'     \item primitives -> logical/integer/double/character/NULL
#'   }
#'
#' @examples
#' # Parse simple object
#' from_toon('name: "Alice"\nage: 30')
#'
#' # Parse array
#' from_toon('[3]:\n  - 1\n  - 2\n  - 3')
#'
#' # Parse with comments
#' from_toon('# comment\nkey: "value"', allow_comments = TRUE)
#'
#' @export
from_toon <- function(text, strict = TRUE, simplify = TRUE,
                      allow_comments = TRUE, allow_duplicate_keys = TRUE) {
  if (is.raw(text)) {
    # Already raw
  } else if (is.character(text)) {
    if (length(text) != 1) {
      stop("text must be a single character string")
    }
  } else {
    stop("text must be a character string or raw vector")
  }

  .Call(C_from_toon, text, strict, simplify, allow_comments, allow_duplicate_keys)
}

#' Read TOON from file
#'
#' @param file Character scalar. Path to TOON file.
#' @param strict Logical. If TRUE (default), enforce strict TOON syntax.
#' @param simplify Logical. If TRUE (default), simplify homogeneous arrays to
#'   atomic vectors.
#' @param allow_comments Logical. If TRUE (default), allow # and // comments.
#' @param allow_duplicate_keys Logical. If TRUE (default), allow duplicate keys
#'   in objects.
#' @param encoding Character. File encoding (default "UTF-8").
#'
#' @return R object representing the parsed TOON data.
#'
#' @examples
#' # Read from file
#' \dontrun{
#' data <- read_toon("config.toon")
#' }
#'
#' @export
read_toon <- function(file, strict = TRUE, simplify = TRUE,
                      allow_comments = TRUE, allow_duplicate_keys = TRUE,
                      encoding = "UTF-8") {
  check_string(file)
  file <- normalizePath(file, mustWork = TRUE)

  if (!identical(encoding, "UTF-8")) {
    warning("encoding parameter is reserved for future use; currently only UTF-8 is supported")
  }

  .Call(C_read_toon, file, strict, simplify, allow_comments, allow_duplicate_keys)
}

#' Serialize R object to TOON
#'
#' Encodes an R value as a TOON document following spec v4.4: canonical
#' numbers, minimal quoting, inline primitive arrays, tabular arrays for
#' uniform objects (with nested field groups) and keyed tables for objects of
#' uniform objects. The output has no trailing newline.
#'
#' @param x R object to serialize.
#' @param pretty Deprecated and ignored: output is always multi-line.
#'   `pretty = FALSE` used to produce invalid TOON; passing anything other than
#'   TRUE warns.
#' @param indent Integer. Spaces per indentation level (the spec's
#'   `indentSize`, default 2).
#' @param strict Deprecated and ignored: NaN and Inf are always written as
#'   `null`, as the spec requires. Supplying it warns.
#' @param allow_comments Deprecated and ignored: encoders never emit comments.
#'   Passing TRUE warns.
#' @param delimiter Document delimiter for inline arrays and tabular rows:
#'   `","` (default), `"\t"` or `"|"`; `"comma"`, `"tab"` and `"pipe"` also
#'   work.
#'
#' @return Character scalar with class "toon" containing the TOON document.
#'
#' @details
#' R values map to the JSON data model as follows:
#' \itemize{
#'   \item `NULL`, `NA`, `NaN`, `Inf` -> `null`
#'   \item a length-1 atomic vector -> a primitive; wrap it in [I()] to write
#'     a one-element array
#'   \item other atomic vectors -> arrays (names are dropped)
#'   \item a list with names -> an object (names must be unique); a list
#'     without names -> an array
#'   \item a data.frame -> an array of row objects, written in tabular form
#'     when every column is atomic (data.frame columns become nested field
#'     groups)
#'   \item factor -> its level; Date -> `"YYYY-MM-DD"`; POSIXct ->
#'     `"YYYY-MM-DDTHH:MM:SSZ"` in UTC (whole seconds);
#'     `bit64::integer64` -> integer
#'   \item doubles use the shortest text that reads back to the same value
#'     (`1` not `1.0`, `0.1`, `1e-7`)
#'   \item other types (functions, environments, raw, complex) -> `null`
#' }
#'
#' @examples
#' to_toon(list(name = "Alice", age = 30, tags = c("a", "b")))
#'
#' to_toon(data.frame(id = 1:2, name = c("Ada", "Bob")))
#'
#' to_toon(list(x = I(1)), delimiter = "|")
#'
#' @export
to_toon <- function(x, pretty = TRUE, indent = 2L, strict = TRUE,
                    allow_comments = FALSE, delimiter = ",") {
  warn_pretty_deprecated(pretty)
  if (!missing(strict)) warn_encoder_strict_deprecated()
  if (!isFALSE(allow_comments)) {
    warning("`allow_comments` is deprecated and ignored: encoders never emit comments.")
  }
  indent <- check_indent(indent)
  delimiter <- check_delimiter(delimiter)

  .Call(C_to_toon, x, indent, delimiter)
}

#' Write R object to TOON file
#'
#' Encodes `x` exactly like [to_toon()] and writes it to `file` (UTF-8, no
#' trailing newline) without building the whole text in memory.
#'
#' @inheritParams to_toon
#' @param file Character scalar. Path to output file.
#'
#' @return Invisibly returns NULL.
#'
#' @examples
#' f <- tempfile(fileext = ".toon")
#' write_toon(list(x = 1, y = 2), f)
#' readLines(f, warn = FALSE)
#'
#' @export
write_toon <- function(x, file, pretty = TRUE, indent = 2L, strict = TRUE,
                       delimiter = ",") {
  check_string(file)
  warn_pretty_deprecated(pretty)
  if (!missing(strict)) warn_encoder_strict_deprecated()
  indent <- check_indent(indent)
  delimiter <- check_delimiter(delimiter)

  .Call(C_write_toon, x, file, indent, delimiter)
  invisible(NULL)
}

#' Print method for toon class
#'
#' @param x TOON string object
#' @param ... Additional arguments (ignored)
#'
#' @return The input `x`, invisibly.
#'
#' @examples
#' x <- to_toon(list(a = 1, b = 2))
#' print(x)
#'
#' @export
print.toon <- function(x, ...) {
  cat(x, sep = "\n")
  invisible(x)
}
