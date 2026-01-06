#' @title TOON Validation and Formatting
#' @name validation
#' @description Functions for validating and formatting TOON data.
NULL

#' Validate TOON
#'
#' @param x Character scalar containing TOON text, or file path if is_file=TRUE.
#' @param is_file Logical. If TRUE, x is treated as a file path.
#' @param strict Logical. If TRUE (default), enforce strict TOON syntax.
#' @param allow_comments Logical. If TRUE (default), allow # and // comments.
#' @param allow_duplicate_keys Logical. If TRUE (default), allow duplicate keys.
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
#' May warn for permissive recoveries.
#'
#' @examples
#' # Valid TOON
#' validate_toon('key: "value"')
#'
#' # Invalid TOON (returns FALSE with error attribute)
#' result <- validate_toon('key: {invalid')
#' if (!result) print(attr(result, "error")$message)
#'
#' @export
validate_toon <- function(x, is_file = FALSE, strict = TRUE,
                          allow_comments = TRUE, allow_duplicate_keys = TRUE) {
  if (!is.character(x) || length(x) != 1) {
    stop("x must be a single character string")
  }

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

  .Call(C_validate_toon, x, is_file, strict, allow_comments, allow_duplicate_keys)
}

#' Assert TOON validity
#'
#' @param x Character scalar containing TOON text, or file path if is_file=TRUE.
#' @param is_file Logical. If TRUE, x is treated as a file path.
#' @param strict Logical. If TRUE (default), enforce strict TOON syntax.
#' @param allow_comments Logical. If TRUE (default), allow # and // comments.
#' @param allow_duplicate_keys Logical. If TRUE (default), allow duplicate keys.
#'
#' @return Invisibly returns TRUE if valid.
#'
#' @details
#' Throws \code{toonlite_parse_error} condition if invalid, with attached
#' location and snippet information.
#'
#' @examples
#' # Valid TOON (returns invisibly)
#' assert_toon('name: "Alice"')
#'
#' # Invalid TOON (throws error)
#' \dontrun{
#' assert_toon('invalid: {')
#' }
#' @export
assert_toon <- function(x, is_file = FALSE, strict = TRUE,
                        allow_comments = TRUE, allow_duplicate_keys = TRUE) {
  result <- validate_toon(x, is_file = is_file, strict = strict,
                          allow_comments = allow_comments,
                          allow_duplicate_keys = allow_duplicate_keys)

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
#' @param x Character scalar containing TOON text, or file path if is_file=TRUE.
#' @param is_file Logical. If TRUE, x is treated as a file path.
#' @param indent Integer. Number of spaces for indentation (default 2).
#' @param canonical Logical. If TRUE, use stable representation with
#'   lexicographic key ordering. Default FALSE (preserve original order).
#' @param allow_comments Logical. If TRUE (default), allow comments in input.
#'
#' @return Character scalar with formatted TOON. If is_file=TRUE, returns
#'   formatted text (does not rewrite file).
#'
#' @examples
#' # Format with default indent
#' format_toon('name:"Alice"')
#'
#' # Format with canonical key ordering
#' format_toon('b: 1\na: 2', canonical = TRUE)
#'
#' @export
format_toon <- function(x, is_file = FALSE, indent = 2L, canonical = FALSE,
                        allow_comments = TRUE) {
  if (!is.character(x) || length(x) != 1) {
    stop("x must be a single character string")
  }

  if (is_file) {
    x <- normalizePath(x, mustWork = TRUE)
  }

  indent <- as.integer(indent)
  if (indent < 0) indent <- 0L

  .Call(C_format_toon, x, is_file, indent, canonical, allow_comments)
}

#' Peek at TOON file structure
#'
#' @param file Character scalar. Path to TOON file.
#' @param n Integer. Number of lines to preview (default 50).
#' @param allow_comments Logical. If TRUE (default), allow comments.
#'
#' @return A list with components:
#'   \itemize{
#'     \item type: Character. Top-level type ("object", "array", "tabular_array", "unknown").
#'     \item first_keys: Character vector. First few keys if top-level is object.
#'     \item preview: Character vector. First n lines.
#'   }
#'
#' @examples
#' \dontrun{
#' info <- toon_peek("data.toon")
#' cat("Type:", info$type, "\n")
#' cat("Preview:\n", info$preview, sep = "\n")
#' }
#'
#' @export
toon_peek <- function(file, n = 50L, allow_comments = TRUE) {
  if (!is.character(file) || length(file) != 1) {
    stop("file must be a single character string")
  }
  file <- normalizePath(file, mustWork = TRUE)

  n <- as.integer(n)
  if (n < 1) n <- 1L

  .Call(C_toon_peek, file, n, allow_comments)
}

#' Get TOON file info
#'
#' @param file Character scalar. Path to TOON file.
#' @param allow_comments Logical. If TRUE (default), allow comments.
#'
#' @return A list with components:
#'   \itemize{
#'     \item array_count: Integer. Number of arrays in file.
#'     \item object_count: Integer. Number of objects in file.
#'     \item has_tabular: Logical. Whether file contains tabular arrays.
#'     \item declared_rows: Integer or NA. Declared row count if tabular.
#'   }
#'
#' @examples
#' \dontrun{
#' info <- toon_info("data.toon")
#' cat("Arrays:", info$array_count, "\n")
#' cat("Has tabular:", info$has_tabular, "\n")
#' }
#'
#' @export
toon_info <- function(file, allow_comments = TRUE) {
  if (!is.character(file) || length(file) != 1) {
    stop("file must be a single character string")
  }
  file <- normalizePath(file, mustWork = TRUE)

  .Call(C_toon_info, file, allow_comments)
}
