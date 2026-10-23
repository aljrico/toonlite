#' @title Tabular TOON I/O (data.frame)
#' @name tabular-io
#' @description Functions for reading and writing tabular TOON data as data.frames.
NULL

#' Read tabular TOON to data.frame
#'
#' Reads a tabular array (`[N]{fields}:` followed by one row per line) into a
#' data.frame, without building the intermediate list that [read_toon()]
#' returns. Column types are inferred from the values: integers mixed with
#' doubles give a double column, and anything else mixed (strings with
#' numbers, booleans with numbers) a character column holding the values'
#' canonical TOON text (`"true"`, `"1.5"`). Booleans mixed with numbers also
#' warn. Nested field groups (`{id,customer{name}}`)
#' become columns named by their dotted path (`customer.name`).
#'
#' @param file Character scalar. Path to TOON file.
#' @param key Character scalar or NULL. If NULL, the document root must be a
#'   tabular array. Otherwise the root must be an object and the tabular array
#'   is read from the field `key` (`key[N]{fields}:`).
#' @param strict Logical. If TRUE (default), apply the spec's strict decoding
#'   rules: declared row counts must match, indentation must be a multiple of
#'   `indent`, tabs in indentation and blank lines between rows are errors. If
#'   FALSE, the spec's non-strict recoveries apply (a count mismatch warns).
#' @param allow_comments Deprecated and ignored. Full-line `#` comments are part
#'   of TOON and are always skipped; passing anything other than TRUE warns.
#' @param allow_duplicate_keys Deprecated and ignored; it has no effect on
#'   tabular reads. Passing anything other than TRUE gives a warning.
#' @param warn Logical. If TRUE (default), emit warnings for recoveries such
#'   as a row count mismatch accepted under `n_mismatch = "warn"`, and for
#'   columns that mix booleans and numbers.
#' @param col_types Named character vector specifying column types:
#'   "logical", "integer", "double", or "character". A "character" column keeps
#'   unquoted values exactly as written.
#' @param ragged_rows Character. How to handle rows whose width differs from
#'   the header:
#'   \itemize{
#'     \item "error" (default, as the spec requires).
#'     \item "expand_warn": toonlite extension, not TOON conformant. Fill
#'       missing fields with NA, add columns `V<n>` for extra fields and warn.
#'   }
#' @param n_mismatch Character. How to handle a declared row count `[N]` that
#'   differs from the rows found, in strict mode:
#'   \itemize{
#'     \item "error" (default, as the spec requires).
#'     \item "warn": toonlite extension, not TOON conformant. Return the rows
#'       found and warn.
#'   }
#' @param max_extra_cols Numeric. Maximum number of columns `ragged_rows =
#'   "expand_warn"` may add. Default Inf (no limit).
#' @param indent Integer. Spaces per indentation level (the spec's
#'   `indentSize`, default 2).
#'
#' @return A base data.frame.
#'
#' @examples
#' f <- tempfile(fileext = ".toon")
#' writeLines(c("[2]{id,name}:", "  1,Alice", "  2,Bob"), f)
#' read_toon_df(f)
#'
#' writeLines(c("users[2]{id,name}:", "  1,Alice", "  2,Bob"), f)
#' read_toon_df(f, key = "users")
#'
#' @export
read_toon_df <- function(file, key = NULL, strict = TRUE, allow_comments = TRUE,
                         allow_duplicate_keys = TRUE, warn = TRUE, col_types = NULL,
                         ragged_rows = c("error", "expand_warn"),
                         n_mismatch = c("error", "warn"),
                         max_extra_cols = Inf, indent = 2L) {
  check_string(file)
  file <- normalizePath(file, mustWork = TRUE)
  if (!is.null(key)) check_string(key)

  ragged_rows <- match.arg(ragged_rows)
  n_mismatch <- match.arg(n_mismatch)
  warn_comments_deprecated(allow_comments)
  warn_duplicate_keys_deprecated(allow_duplicate_keys)
  check_col_types(col_types)
  indent <- check_indent(indent)

  .Call(C_read_toon_df, file, key, isTRUE(strict), indent, isTRUE(warn),
        col_types, ragged_rows, n_mismatch, max_extra_cols)
}

#' Write data.frame to tabular TOON
#'
#' Writes `df` as a root tabular array (`[N]{fields}:` and one row per line),
#' exactly as [write_toon()] writes a data.frame. A data.frame with no rows is
#' written as the empty array `[]`.
#'
#' @inheritParams write_toon
#' @param df A data.frame to write.
#' @param tabular Deprecated and ignored. Data frames are always written as a
#'   tabular TOON array; passing anything other than TRUE gives a warning.
#'
#' @return Invisibly returns NULL.
#'
#' @examples
#' f <- tempfile(fileext = ".toon")
#' write_toon_df(head(mtcars[, 1:4], 3), f)
#' readLines(f, warn = FALSE)
#'
#' @export
write_toon_df <- function(df, file, tabular = TRUE, pretty = TRUE,
                          indent = 2L, strict = TRUE, delimiter = ",") {
  if (!is.data.frame(df)) {
    stop("df must be a data.frame")
  }
  check_string(file)

  warn_tabular_deprecated(tabular)
  warn_pretty_deprecated(pretty)
  if (!missing(strict)) warn_encoder_strict_deprecated()
  indent <- check_indent(indent)
  delimiter <- check_delimiter(delimiter)

  .Call(C_write_toon, df, file, indent, delimiter)

  invisible(NULL)
}

#' Convert array-of-objects to tabular representation
#'
#' @param x List of lists/named lists (array of objects).
#' @param strict Logical. If TRUE (default), validate input structure.
#' @param warn Logical. If TRUE (default), emit warnings for type promotions.
#'
#' @return A data.frame suitable for \code{write_toon_df()}.
#'
#' @examples
#' # Convert array of objects to data.frame
#' records <- list(
#'   list(name = "Alice", age = 30),
#'   list(name = "Bob", age = 25)
#' )
#' df <- as_tabular_toon(records)
#'
#' @details
#' In permissive mode:
#' \itemize{
#'   \item Union schema across all records
#'   \item Missing fields filled with NA
#'   \item Type promotion to accommodate all values
#' }
#'
#' @export
as_tabular_toon <- function(x, strict = TRUE, warn = TRUE) {
  if (!is.list(x)) {
    stop("x must be a list")
  }

  if (length(x) == 0) {
    return(data.frame())
  }

  # Check all elements are lists/named lists
  all_lists <- all(vapply(x, is.list, logical(1)))
  if (!all_lists) {
    if (strict) {
      stop("All elements must be lists (objects)")
    }
    # Try to coerce
    x <- lapply(x, as.list)
  }

  # Collect all unique field names (union schema)
  all_names <- unique(unlist(lapply(x, names)))

  if (length(all_names) == 0 && strict) {
    stop("No named fields found in input")
  }

  if (length(all_names) == 0) {
    # Unnamed list elements - treat as columns V1, V2, ...
    max_len <- max(vapply(x, length, integer(1)))
    all_names <- paste0("V", seq_len(max_len))
  }

  # Build data.frame with union schema
  result <- vector("list", length(all_names))
  names(result) <- all_names

  for (name in all_names) {
    col_values <- lapply(x, function(obj) {
      if (name %in% names(obj)) {
        obj[[name]]
      } else {
        NA
      }
    })

    # Determine best type
    non_na <- col_values[!is.na(col_values)]

    if (length(non_na) == 0) {
      result[[name]] <- rep(NA, length(x))
    } else {
      # Try to simplify to atomic vector
      types <- vapply(non_na, function(v) {
        if (is.null(v)) "null"
        else if (is.logical(v)) "logical"
        else if (is.integer(v)) "integer"
        else if (is.numeric(v)) "double"
        else "character"
      }, character(1))

      unique_types <- unique(types[types != "null"])

      if (length(unique_types) == 0) {
        result[[name]] <- rep(NA, length(x))
      } else if (length(unique_types) == 1) {
        # Homogeneous type
        result[[name]] <- unlist(col_values)
      } else {
        # Mixed types - promote to character
        if (warn) {
          warning("Column '", name, "' has mixed types, promoting to character")
        }
        result[[name]] <- vapply(col_values, function(v) {
          if (is.null(v) || (length(v) == 1 && is.na(v))) NA_character_
          else as.character(v)
        }, character(1))
      }
    }
  }

  as.data.frame(result, stringsAsFactors = FALSE)
}
