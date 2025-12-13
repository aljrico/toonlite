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
#' @export
read_toon <- function(file, strict = TRUE, simplify = TRUE,
                      allow_comments = TRUE, allow_duplicate_keys = TRUE,
                      encoding = "UTF-8") {
  if (!is.character(file) || length(file) != 1) {
    stop("file must be a single character string")
  }
  file <- normalizePath(file, mustWork = TRUE)

  .Call(C_read_toon, file, strict, simplify, allow_comments, allow_duplicate_keys)
}

#' Serialize R object to TOON
#'
#' @param x R object to serialize.
#' @param pretty Logical. If TRUE (default), use multi-line formatting.
#' @param indent Integer. Number of spaces for indentation (default 2).
#' @param strict Logical. If TRUE (default), reject NaN/Inf values.
#' @param allow_comments Logical. For future use (writers generally should not
#'   emit comments).
#'
#' @return Character scalar with class "toon" containing the TOON representation.
#'
#' @details
#' Type conversions:
#' \itemize{
#'   \item factor -> character
#'   \item Date -> ISO 8601 string (YYYY-MM-DD)
#'   \item POSIXct -> ISO 8601 string (YYYY-MM-DDTHH:MM:SSZ)
#'   \item NA values -> null
#' }
#'
#' @export
to_toon <- function(x, pretty = TRUE, indent = 2L, strict = TRUE,
                    allow_comments = FALSE) {
  indent <- as.integer(indent)
  if (indent < 0) indent <- 0L

  .Call(C_to_toon, x, pretty, indent, strict)
}

#' Write R object to TOON file
#'
#' @param x R object to serialize.
#' @param file Character scalar. Path to output file.
#' @param pretty Logical. If TRUE (default), use multi-line formatting.
#' @param indent Integer. Number of spaces for indentation (default 2).
#' @param strict Logical. If TRUE (default), reject NaN/Inf values.
#'
#' @return Invisibly returns NULL.
#'
#' @export
write_toon <- function(x, file, pretty = TRUE, indent = 2L, strict = TRUE) {
  if (!is.character(file) || length(file) != 1) {
    stop("file must be a single character string")
  }

  toon_str <- to_toon(x, pretty = pretty, indent = indent, strict = strict)

  writeLines(toon_str, file, useBytes = TRUE)
  invisible(NULL)
}

#' Print method for toon class
#'
#' @param x TOON string object
#' @param ... Additional arguments (ignored)
#'
#' @export
print.toon <- function(x, ...) {
  cat(x, sep = "\n")
  invisible(x)
}
