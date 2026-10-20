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

#' Stream array items
#'
#' Decodes the items of an array one batch at a time and hands each batch to
#' `callback`, so arrays larger than memory can be processed. Items can be
#' any TOON value; rows of a tabular array arrive as row objects (named
#' lists). For a data.frame per batch use [toon_stream_rows()].
#'
#' @param file Character scalar. Path to TOON file.
#' @param key Character scalar or NULL. If NULL, the document root must be an
#'   array. Otherwise the root must be an object and the array is read from
#'   its field `key`.
#' @param callback Function called with each batch: a list of up to
#'   `batch_size` items, each decoded as by [read_toon()].
#' @param batch_size Integer. Number of items per batch (default 1000).
#' @inheritParams from_toon
#' @param warn Currently unused: decoding items produces no warnings.
#'
#' @return Invisibly returns NULL.
#'
#' @details
#' The rest of the document is decoded and validated as well, but only the
#' streamed items are handed out. An error found after some batches were
#' delivered (a declared count that does not match in strict mode, or invalid
#' content later in the file) is raised after those callbacks ran.
#'
#' @examples
#' f <- tempfile(fileext = ".toon")
#' write_toon(list(items = list(1, "two", list(x = 3))), f)
#' toon_stream_items(f, key = "items", batch_size = 2, callback = function(batch) {
#'   str(batch)
#' })
#'
#' @export
toon_stream_items <- function(file, key = NULL, callback, batch_size = 1000L,
                              strict = TRUE, allow_comments = TRUE,
                              allow_duplicate_keys = TRUE, warn = TRUE,
                              simplify = TRUE, indent = 2L) {
  check_string(file)
  file <- normalizePath(file, mustWork = TRUE)
  if (!is.null(key)) check_string(key)

  if (!is.function(callback)) {
    stop("callback must be a function")
  }
  warn_comments_deprecated(allow_comments)
  if (!missing(allow_duplicate_keys)) warn_decoder_duplicate_keys_deprecated()
  indent <- check_indent(indent)
  batch_size <- as.integer(batch_size)
  if (is.na(batch_size) || batch_size < 1L) batch_size <- 1L

  .Call(C_stream_items, file, key, callback, batch_size, isTRUE(strict),
        indent, isTRUE(simplify))
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
#' @param indent Integer. Spaces per indentation level (default 2).
#' @param delimiter Document delimiter: `","` (default), `"\t"` or `"|"`.
#'
#' @return Invisibly returns the number of rows written.
#'
#' @details
#' Writes a tabular TOON array without holding all rows in memory. Cells are
#' formatted exactly as [write_toon_df()] formats them, so the file reads back
#' the same way; batch columns must be atomic vectors. The row count goes in
#' the header, so rows are first written to a temporary file and `file` is
#' only written once `row_source()` returns NULL. If `row_source()` fails,
#' `file` is left untouched. With no rows, `file` holds the empty array `[]`.
#'
#' @examples
#' f <- tempfile(fileext = ".toon")
#' i <- 0
#' row_source <- function() {
#'   i <<- i + 1
#'   if (i > 3) return(NULL)
#'   data.frame(x = i, y = i * 2)
#' }
#' toon_stream_write_rows(f, schema = c("x", "y"), row_source = row_source)
#' readLines(f, warn = FALSE)
#'
#' @export
toon_stream_write_rows <- function(file, schema, row_source,
                                   batch_size = 10000L, indent = 2L,
                                   delimiter = ",") {
  check_string(file)
  if (!is.character(schema) || length(schema) == 0 || anyNA(schema)) {
    stop("schema must be a non-empty character vector")
  }
  if (anyDuplicated(schema)) {
    stop("schema must not contain duplicate names")
  }
  if (!is.function(row_source)) {
    stop("row_source must be a function")
  }

  if (!missing(batch_size)) {
    warning("`batch_size` is deprecated and ignored: row_source() decides ",
            "how many rows each batch has.")
  }

  indent <- check_indent(indent)
  delimiter <- check_delimiter(delimiter)

  tmp <- tempfile("toonlite-rows-")
  writer_ptr <- .Call(C_stream_write_init, file, tmp, schema, indent, delimiter)
  committed <- FALSE
  on.exit(if (!committed) .Call(C_stream_write_close, writer_ptr, FALSE))

  repeat {
    batch <- row_source()
    if (is.null(batch)) break
    if (!is.data.frame(batch)) {
      stop("row_source must return a data.frame or NULL")
    }
    if (nrow(batch) == 0) next
    if (!identical(names(batch), schema)) {
      missing_cols <- setdiff(schema, names(batch))
      if (length(missing_cols) > 0) {
        stop("Batch is missing schema columns: ",
             paste(missing_cols, collapse = ", "))
      }
      batch <- batch[schema]
    }

    .Call(C_stream_write_batch, writer_ptr, batch)
  }

  committed <- TRUE
  total_rows <- .Call(C_stream_write_close, writer_ptr, TRUE)
  invisible(total_rows)
}
