#' @title Core TOON I/O functions
#' @name core-io
#' @description Functions for parsing and serializing TOON data.
NULL

#' Parse TOON from string
#'
#' Decodes a TOON document (spec v4.4) into R values.
#'
#' @param text Character scalar or raw vector containing TOON data. If raw,
#'   treated as UTF-8 bytes.
#' @param strict Logical. If TRUE (default), apply the spec's strict decoding
#'   rules: declared lengths must match, indentation must be a multiple of
#'   `indent` with no tabs, blank lines inside arrays and duplicate keys are
#'   errors. If FALSE, the spec's five non-strict recoveries apply instead
#'   (count mismatches accepted, duplicate keys resolve last-write-wins,
#'   tab/odd indentation, blank lines and depth jumps tolerated).
#' @param simplify Logical. If TRUE (default), arrays whose elements are all
#'   primitives of one type become atomic vectors (`null` becomes `NA`;
#'   integers mixed with doubles give a double vector).
#' @param allow_comments Deprecated and ignored. Full-line `#` comments are part
#'   of TOON and are always skipped; passing anything other than TRUE warns.
#' @param allow_duplicate_keys Deprecated and ignored. Duplicate keys are an
#'   error when `strict = TRUE` and resolve last-write-wins when
#'   `strict = FALSE`, as the spec requires. Supplying it warns.
#' @param indent Integer. Spaces per indentation level (the spec's
#'   `indentSize`, default 2).
#'
#' @return R object representing the parsed TOON data:
#'   \itemize{
#'     \item object -> named list (an empty object is a named list of length 0)
#'     \item array -> list, or atomic vector when simplified
#'     \item string -> character; `true`/`false` -> logical; `null` -> `NULL`
#'     \item number -> integer when it is a whole number between -2147483647
#'       and 2147483647, otherwise double. Numbers beyond the double range
#'       become `-Inf`/`Inf`.
#'   }
#'
#' @details
#' Nesting deeper than 1000 levels is an error. Strings containing NUL
#' (`\u0000`) cannot be represented in R and are an error.
#'
#' With `strict = FALSE`, a declared length that does not match the items
#' found is accepted with a warning (the first five are reported).
#'
#' @examples
#' from_toon("name: Alice\nage: 30")
#'
#' from_toon("[3]: 1,2,3")
#'
#' from_toon("users[2]{id,name}:\n  1,Ada\n  2,Bob")
#'
#' # Comment lines are skipped
#' from_toon("# settings\nkey: value")
#'
#' @export
from_toon <- function(text, strict = TRUE, simplify = TRUE,
                      allow_comments = TRUE, allow_duplicate_keys = TRUE,
                      indent = 2L) {
  if (is.raw(text)) {
    # Already raw
  } else if (is.character(text)) {
    if (length(text) != 1) {
      stop("text must be a single character string")
    }
  } else {
    stop("text must be a character string or raw vector")
  }
  warn_comments_deprecated(allow_comments)
  if (!missing(allow_duplicate_keys)) warn_decoder_duplicate_keys_deprecated()
  indent <- check_indent(indent)

  .Call(C_from_toon, text, isTRUE(strict), isTRUE(simplify), indent)
}

#' Read TOON from file
#'
#' @inheritParams from_toon
#' @param file Character scalar. Path to TOON file.
#' @param encoding Character. File encoding (default "UTF-8").
#'
#' @return R object representing the parsed TOON data; see [from_toon()].
#'
#' @details
#' With `strict = FALSE`, a declared length that does not match the items
#' found is accepted with a warning, as in [read_toon_df()].
#'
#' @examples
#' f <- tempfile(fileext = ".toon")
#' write_toon(list(a = 1, b = c("x", "y")), f)
#' read_toon(f)
#'
#' @export
read_toon <- function(file, strict = TRUE, simplify = TRUE,
                      allow_comments = TRUE, allow_duplicate_keys = TRUE,
                      encoding = "UTF-8", indent = 2L) {
  check_string(file)
  file <- normalizePath(file, mustWork = TRUE)

  if (!identical(encoding, "UTF-8")) {
    warning("encoding parameter is reserved for future use; currently only UTF-8 is supported")
  }
  warn_comments_deprecated(allow_comments)
  if (!missing(allow_duplicate_keys)) warn_decoder_duplicate_keys_deprecated()
  indent <- check_indent(indent)

  .Call(C_read_toon, file, isTRUE(strict), isTRUE(simplify), indent)
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
#' Nesting deeper than 1000 levels is an error, as it is when reading, so
#' everything written can be read back.
#'
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
