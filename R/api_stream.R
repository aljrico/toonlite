#' @title Streaming TOON I/O
#' @name streaming
#' @description Functions for streaming TOON data in batches.
NULL

#' Stream tabular TOON rows
#'
#' Reads a tabular array in batches of rows, calling `callback` with each batch
#' as a data.frame. Memory use is bounded by the batch size, not the file size.
#' Rows are typed exactly as in [read_toon_df()], per batch.
#'
#' @inheritParams read_toon_df
#' @param callback Function. Called with each batch as a data.frame.
#'   Return value is ignored; errors propagate and stop the read.
#' @param batch_size Integer. Number of rows per batch (default 10000).
#'
#' @return Invisibly returns NULL.
#'
#' @details
#' The whole table is checked as it is read; a declared row count that does
#' not match is only known at the end, so it is reported after the last
#' batch. Column order is stable within a stream; with
#' `ragged_rows = "expand_warn"`, later batches may have extra columns.
#'
#' @examples
#' f <- tempfile(fileext = ".toon")
#' writeLines(c("[3]{id,name}:", "  1,Alice", "  2,Bob", "  3,Carol"), f)
#' toon_stream_rows(f, callback = function(batch) print(nrow(batch)),
#'                  batch_size = 2)
#'
#' @export
toon_stream_rows <- function(file, key = NULL, callback, batch_size = 10000L,
                             strict = TRUE, allow_comments = TRUE,
                             allow_duplicate_keys = TRUE, warn = TRUE,
                             col_types = NULL,
                             ragged_rows = c("error", "expand_warn"),
                             n_mismatch = c("error", "warn"),
                             max_extra_cols = Inf, indent = 2L) {
  check_string(file)
  file <- normalizePath(file, mustWork = TRUE)
  if (!is.null(key)) check_string(key)

  if (!is.function(callback)) {
    stop("callback must be a function")
  }

  batch_size <- as.integer(batch_size)
  if (is.na(batch_size) || batch_size < 1) batch_size <- 1L

  ragged_rows <- match.arg(ragged_rows)
  n_mismatch <- match.arg(n_mismatch)
  warn_comments_deprecated(allow_comments)
  warn_duplicate_keys_deprecated(allow_duplicate_keys)
  check_col_types(col_types)
  indent <- check_indent(indent)

  .Call(C_stream_rows, file, key, callback, batch_size, isTRUE(strict), indent,
        isTRUE(warn), col_types, ragged_rows, n_mismatch, max_extra_cols)

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
#' @examples
#' \dontrun{
#' # Stream array items
#' toon_stream_items("items.toon",
#'   callback = function(batch) {
#'     cat("Got", length(batch), "items\n")
#'   }
#' )
#' }
#'
#' @export
toon_stream_items <- function(file, key = NULL, callback, batch_size = 1000L,
                              strict = TRUE, allow_comments = TRUE,
                              allow_duplicate_keys = TRUE, warn = TRUE,
                              simplify = TRUE) {
  check_string(file)
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
#' @param row_source Function called with no arguments that returns the next
#'   batch as a data.frame, or NULL to end. It decides how many rows each
#'   batch has.
#' @param batch_size Deprecated and ignored (it was never passed to
#'   `row_source`). Supplying it gives a warning.
#' @param indent Integer. Number of spaces for indentation (default 2).
#'
#' @return Invisibly returns the number of rows written.
#'
#' @details
#' Writes a tabular TOON array without holding all rows in memory. Cells are
#' formatted exactly as [write_toon_df()] formats them, so the file reads back
#' the same way. Non-finite doubles (`NaN`, `Inf`) are written as `null`, as
#' with `write_toon_df(strict = FALSE)`.
#'
#' @examples
#' \dontrun{
#' # Stream write rows
#' i <- 0
#' row_source <- function() {
#'   i <<- i + 1
#'   if (i > 3) return(NULL)
#'   data.frame(x = i, y = i * 2)
#' }
#' toon_stream_write_rows("output.toon",
#'   schema = c("x", "y"),
#'   row_source = row_source
#' )
#' }
#'
#' @export
toon_stream_write_rows <- function(file, schema, row_source,
                                   batch_size = 10000L, indent = 2L) {
  check_string(file)
  if (!is.character(schema) || length(schema) == 0) {
    stop("schema must be a non-empty character vector")
  }
  if (!is.function(row_source)) {
    stop("row_source must be a function")
  }

  if (!missing(batch_size)) {
    warning("`batch_size` is deprecated and ignored: row_source() decides ",
            "how many rows each batch has.")
  }

  indent <- as.integer(indent)
  if (indent < 0) indent <- 0L

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
