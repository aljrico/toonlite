#' @title Tabular TOON I/O (data.frame)
#' @name tabular-io
#' @description Functions for reading and writing tabular TOON data as data.frames.
NULL

#' Read tabular TOON to data.frame
#'
#' @param file Character scalar. Path to TOON file.
#' @param key Character scalar or NULL. If non-NULL, extract tabular array at
#'   root\[key\] (root must be object).
#' @param strict Logical. If TRUE (default), enforce strict TOON syntax.
#' @param allow_comments Logical. If TRUE (default), allow # and // comments.
#' @param allow_duplicate_keys Logical. If TRUE (default), allow duplicate keys.
#' @param warn Logical. If TRUE (default), emit warnings for schema changes.
#' @param col_types Named character vector specifying column types:
#'   "logical", "integer", "double", or "character".
#' @param ragged_rows Character. How to handle rows with different field counts:
#'   \itemize{
#'     \item "expand_warn" (default): Fill missing fields with NA, expand schema
#'       for extra fields, emit aggregated warning.
#'     \item "error": Require all rows have same field count.
#'   }
#' @param n_mismatch Character. How to handle declared row count mismatch:
#'   \itemize{
#'     \item "warn" (default): Accept mismatch, return observed rows, emit warning.
#'     \item "error": Require observed row count equals declared \[N\].
#'   }
#' @param max_extra_cols Numeric. Maximum new columns allowed via schema expansion.
#'   Default Inf (no limit).
#'
#' @return A base data.frame.
#'
#' @examples
#' \dontrun{
#' # Read tabular TOON file
#' df <- read_toon_df("data.toon")
#'
#' # Read nested tabular array
#' df <- read_toon_df("config.toon", key = "records")
#' }
#'
#' @export
read_toon_df <- function(file, key = NULL, strict = TRUE, allow_comments = TRUE,
                         allow_duplicate_keys = TRUE, warn = TRUE, col_types = NULL,
                         ragged_rows = c("expand_warn", "error"),
                         n_mismatch = c("warn", "error"),
                         max_extra_cols = Inf) {
  if (!is.character(file) || length(file) != 1) {
    stop("file must be a single character string")
  }
  file <- normalizePath(file, mustWork = TRUE)

  ragged_rows <- match.arg(ragged_rows)
  n_mismatch <- match.arg(n_mismatch)

  if (!is.null(col_types)) {
    if (!is.character(col_types) || is.null(names(col_types))) {
      stop("col_types must be a named character vector")
    }
  }

  .Call(C_read_toon_df, file, key, strict, allow_comments, allow_duplicate_keys,
        warn, col_types, ragged_rows, n_mismatch, max_extra_cols)
}

#' Write data.frame to tabular TOON
#'
#' @param df A data.frame to write.
#' @param file Character scalar. Path to output file.
#' @param tabular Logical. If TRUE (default), write as tabular TOON array.
#' @param pretty Logical. If TRUE (default), use multi-line formatting.
#' @param indent Integer. Number of spaces for indentation (default 2).
#' @param strict Logical. If TRUE (default), reject NaN/Inf values.
#'
#' @return Invisibly returns NULL.
#'
#' @examples
#' \dontrun{
#' # Write data.frame as tabular TOON
#' write_toon_df(mtcars[1:3, 1:4], "cars.toon")
#' }
#'
#' @export
write_toon_df <- function(df, file, tabular = TRUE, pretty = TRUE,
                          indent = 2L, strict = TRUE) {
  if (!is.data.frame(df)) {
    stop("df must be a data.frame")
  }
  if (!is.character(file) || length(file) != 1) {
    stop("file must be a single character string")
  }

  indent <- as.integer(indent)
  if (indent < 0) indent <- 0L

  .Call(C_write_toon_df, df, file, tabular, pretty, indent, strict)

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
