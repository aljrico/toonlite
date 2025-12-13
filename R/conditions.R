#' @title toonlite error and warning conditions
#' @name conditions
#' @description Functions for creating and signaling toonlite-specific conditions.
NULL

#' Create a toonlite parse error condition
#'
#' @param message Error message
#' @param line Line number where error occurred (1-indexed)
#' @param column Column number where error occurred (1-indexed)
#' @param snippet Code snippet showing error context
#' @param file File path if applicable
#' @return A condition object of class c("toonlite_parse_error", "error", "condition")
#' @keywords internal
new_toonlite_parse_error <- function(message, line = NULL, column = NULL,
                                      snippet = NULL, file = NULL) {
  error_info <- list(
    type = "parse_error",
    message = message,
    line = line,
    column = column,
    snippet = snippet,
    file = file
  )

  # Build detailed message
  full_message <- message
  if (!is.null(file)) {
    full_message <- paste0(full_message, "\n  File: ", file)
  }
  if (!is.null(line)) {
    loc <- paste0("line ", line)
    if (!is.null(column)) {
      loc <- paste0(loc, ", column ", column)
    }
    full_message <- paste0(full_message, "\n  Location: ", loc)
  }
  if (!is.null(snippet) && nzchar(snippet)) {
    full_message <- paste0(full_message, "\n  Snippet: ", snippet)
  }

  structure(
    list(message = full_message, call = NULL, error_info = error_info),
    class = c("toonlite_parse_error", "error", "condition")
  )
}

#' Signal a toonlite parse error
#'
#' @param message Error message
#' @param line Line number
#' @param column Column number
#' @param snippet Code snippet
#' @param file File path
#' @keywords internal
stop_toonlite_parse <- function(message, line = NULL, column = NULL,
                                 snippet = NULL, file = NULL) {
  cond <- new_toonlite_parse_error(message, line, column, snippet, file)
  stop(cond)
}

#' Create a toonlite warning condition
#'
#' @param message Warning message
#' @param type Warning type (e.g., "duplicate_key", "ragged_rows", "n_mismatch")
#' @param details Additional details as a list
#' @return A condition object of class c("toonlite_warning", "warning", "condition")
#' @keywords internal
new_toonlite_warning <- function(message, type = "general", details = NULL) {
  structure(
    list(message = message, call = NULL, type = type, details = details),
    class = c("toonlite_warning", "warning", "condition")
  )
}

#' Signal a toonlite warning
#'
#' @param message Warning message
#' @param type Warning type
#' @param details Additional details
#' @keywords internal
warn_toonlite <- function(message, type = "general", details = NULL) {
  cond <- new_toonlite_warning(message, type, details)
  warning(cond)
}

#' Create a validation error object (not thrown)
#'
#' @param type Error type
#' @param message Error message
#' @param line Line number
#' @param column Column number
#' @param snippet Code snippet
#' @param file File path
#' @return A list with error details
#' @keywords internal
new_validation_error <- function(type, message, line = NULL, column = NULL,
                                  snippet = NULL, file = NULL) {
  list(
    type = type,
    message = message,
    line = line,
    column = column,
    snippet = snippet,
    file = file
  )
}
