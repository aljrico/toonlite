#' @title Streaming TOON I/O
#' @name streaming
#' @description Functions for streaming TOON data in batches.
NULL

#' Stream tabular TOON rows
#'
#' @param file Character scalar. Path to TOON file.
#' @param key Character scalar or NULL. If non-NULL, extract tabular array at
#'   root\[key\].
#' @param callback Function. Called with each batch as a data.frame.
#'   Return value is ignored; exceptions propagate and abort parsing.
#' @param batch_size Integer. Number of rows per batch (default 10000).
#' @param strict Logical. If TRUE (default), enforce strict TOON syntax.
#' @param allow_comments Logical. If TRUE (default), allow # and // comments.
#' @param allow_duplicate_keys Logical. If TRUE (default), allow duplicate keys.
#' @param warn Logical. If TRUE (default), emit warnings.
#' @param col_types Named character vector specifying column types.
#' @param ragged_rows Character. How to handle rows with different field counts.
#' @param n_mismatch Character. How to handle declared row count mismatch.
#' @param max_extra_cols Numeric. Maximum new columns allowed.
#'
#' @return Invisibly returns NULL.
#'
#' @details
#' Guarantees:
#' \itemize{
#'   \item Batches are independent and may be garbage collected
#'   \item Stable column order within stream
#' }
#'
#' @export
toon_stream_rows <- function(file, key = NULL, callback, batch_size = 10000L,
                             strict = TRUE, allow_comments = TRUE,
                             allow_duplicate_keys = TRUE, warn = TRUE,
                             col_types = NULL,
                             ragged_rows = c("expand_warn", "error"),
                             n_mismatch = c("warn", "error"),
                             max_extra_cols = Inf) {
  if (!is.character(file) || length(file) != 1) {
    stop("file must be a single character string")
  }
  file <- normalizePath(file, mustWork = TRUE)

  if (!is.function(callback)) {
    stop("callback must be a function")
  }

  batch_size <- as.integer(batch_size)
  if (batch_size < 1) batch_size <- 1L

  ragged_rows <- match.arg(ragged_rows)
  n_mismatch <- match.arg(n_mismatch)

  .Call(C_stream_rows, file, key, callback, batch_size, strict, allow_comments,
        allow_duplicate_keys, warn, col_types, ragged_rows, n_mismatch,
        max_extra_cols)

  invisible(NULL)
}

#' Stream non-tabular array items
#'
#' @param file Character scalar. Path to TOON file.
#' @param key Character scalar or NULL. If non-NULL, extract array at root\[key\].
#' @param callback Function. Called with each batch as a list.
#' @param batch_size Integer. Number of items per batch (default 1000).
#' @param strict Logical. If TRUE (default), enforce strict TOON syntax.
#' @param allow_comments Logical. If TRUE (default), allow # and // comments.
#' @param allow_duplicate_keys Logical. If TRUE (default), allow duplicate keys.
#' @param warn Logical. If TRUE (default), emit warnings.
#' @param simplify Logical. If TRUE (default), simplify homogeneous batches.
#'
#' @return Invisibly returns NULL.
#'
#' @export
toon_stream_items <- function(file, key = NULL, callback, batch_size = 1000L,
                              strict = TRUE, allow_comments = TRUE,
                              allow_duplicate_keys = TRUE, warn = TRUE,
                              simplify = TRUE) {
  if (!is.character(file) || length(file) != 1) {
    stop("file must be a single character string")
  }
  file <- normalizePath(file, mustWork = TRUE)

  if (!is.function(callback)) {
    stop("callback must be a function")
  }

  # For now, fall back to reading entire file and streaming manually
  # A full implementation would use incremental parsing
  data <- read_toon(file, strict = strict, simplify = simplify,
                    allow_comments = allow_comments,
                    allow_duplicate_keys = allow_duplicate_keys)

  if (!is.null(key)) {
    if (!is.list(data) || !(key %in% names(data))) {
      stop("Key '", key, "' not found in root object")
    }
    data <- data[[key]]
  }

  # Accept both lists and atomic vectors (simplified arrays)
  if (!is.list(data) && !is.atomic(data)) {
    stop("Target must be an array (list or vector)")
  }
  # Convert atomic vector to list for consistent batch processing
  if (is.atomic(data)) {
    data <- as.list(data)
  }

  batch_size <- as.integer(batch_size)
  if (batch_size < 1) batch_size <- 1L

  n <- length(data)
  i <- 1L

  while (i <= n) {
    end <- min(i + batch_size - 1L, n)
    batch <- data[i:end]
    callback(batch)
    i <- end + 1L
  }

  invisible(NULL)
}

#' Stream write tabular rows
#'
#' @param file Character scalar. Path to output file.
#' @param schema Character vector of column names.
#' @param row_source Function that returns next batch as data.frame, or NULL
#'   to end.
#' @param batch_size Integer. Hint for batch size (passed to row_source).
#' @param indent Integer. Number of spaces for indentation (default 2).
#'
#' @return Invisibly returns the number of rows written.
#'
#' @details
#' Writes a tabular TOON array without holding all rows in memory.
#'
#' @export
toon_stream_write_rows <- function(file, schema, row_source,
                                   batch_size = 10000L, indent = 2L) {
  if (!is.character(file) || length(file) != 1) {
    stop("file must be a single character string")
  }
  if (!is.character(schema) || length(schema) == 0) {
    stop("schema must be a non-empty character vector")
  }
  if (!is.function(row_source)) {
    stop("row_source must be a function")
  }

  indent <- as.integer(indent)
  if (indent < 0) indent <- 0L
  batch_size <- as.integer(batch_size)
  if (batch_size < 1) batch_size <- 1L

  # Initialize writer
  writer_ptr <- .Call(C_stream_write_init, file, schema, indent)

  total_rows <- 0L

  tryCatch({
    repeat {
      batch <- row_source()
      if (is.null(batch)) break
      if (!is.data.frame(batch)) {
        stop("row_source must return a data.frame or NULL")
      }
      if (nrow(batch) == 0) next

      .Call(C_stream_write_batch, writer_ptr, batch)
      total_rows <- total_rows + nrow(batch)
    }
  }, finally = {
    .Call(C_stream_write_close, writer_ptr)
  })

  invisible(total_rows)
}
