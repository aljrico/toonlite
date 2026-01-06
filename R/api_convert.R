#' @title TOON Conversion Functions
#' @name conversions
#' @description Functions for converting between TOON and other formats.
NULL

# JSON <-> TOON (lossless lane)

#' Convert JSON to TOON
#'
#' @param json Character scalar containing JSON.
#' @param pretty Logical. If TRUE (default), use multi-line formatting.
#' @param strict Logical. If TRUE (default), enforce strict syntax.
#' @param allow_comments Logical. If TRUE (default), allow comments in output.
#'
#' @return Character scalar with class "toon".
#'
#' @details
#' Requires the jsonlite package. If not installed, an error is thrown.
#'
#' @examples
#' \dontrun{
#' toon <- json_to_toon('{"name": "Alice", "age": 30}')
#' }
#'
#' @export
json_to_toon <- function(json, pretty = TRUE, strict = TRUE,
                         allow_comments = TRUE) {
  if (!requireNamespace("jsonlite", quietly = TRUE)) {
    stop("Install jsonlite to use JSON conversion: install.packages('jsonlite')")
  }

  if (!is.character(json) || length(json) != 1) {
    stop("json must be a single character string")
  }

  # Parse JSON to R object
  obj <- jsonlite::fromJSON(json, simplifyVector = FALSE)

  # Convert to TOON
  to_toon(obj, pretty = pretty, strict = strict, allow_comments = allow_comments)
}

#' Convert TOON to JSON
#'
#' @param toon Character scalar containing TOON.
#' @param pretty Logical. If TRUE, use multi-line JSON formatting (default FALSE).
#' @param strict Logical. If TRUE (default), enforce strict syntax.
#' @param allow_comments Logical. If TRUE (default), allow comments in input.
#'
#' @return Character scalar containing JSON.
#'
#' @details
#' Requires the jsonlite package. If not installed, an error is thrown.
#'
#' @examples
#' \dontrun{
#' json <- toon_to_json('name: "Alice"\nage: 30')
#' }
#'
#' @export
toon_to_json <- function(toon, pretty = FALSE, strict = TRUE,
                         allow_comments = TRUE) {
  if (!requireNamespace("jsonlite", quietly = TRUE)) {
    stop("Install jsonlite to use JSON conversion: install.packages('jsonlite')")
  }

  if (!is.character(toon) || length(toon) != 1) {
    stop("toon must be a single character string")
  }

  # Parse TOON to R object
  obj <- from_toon(toon, strict = strict, simplify = FALSE,
                   allow_comments = allow_comments)

  # Convert to JSON
  jsonlite::toJSON(obj, auto_unbox = TRUE, pretty = pretty, null = "null")
}

# CSV <-> TOON (tabular lane)

#' Convert TOON to CSV
#'
#' @param path_toon Character scalar. Path to input TOON file.
#' @param path_csv Character scalar. Path to output CSV file.
#' @param key Character scalar or NULL. If non-NULL, extract tabular array at
#'   root\[key\].
#' @param strict Logical. If TRUE (default), enforce strict syntax.
#' @param allow_comments Logical. If TRUE (default), allow comments.
#' @param warn Logical. If TRUE (default), emit warnings.
#'
#' @return Invisibly returns NULL.
#'
#' @examples
#' \dontrun{
#' toon_to_csv("data.toon", "data.csv")
#' }
#'
#' @export
toon_to_csv <- function(path_toon, path_csv, key = NULL, strict = TRUE,
                        allow_comments = TRUE, warn = TRUE) {
  if (!is.character(path_toon) || length(path_toon) != 1) {
    stop("path_toon must be a single character string")
  }
  if (!is.character(path_csv) || length(path_csv) != 1) {
    stop("path_csv must be a single character string")
  }

  # Read TOON as data.frame
  df <- read_toon_df(path_toon, key = key, strict = strict,
                     allow_comments = allow_comments, warn = warn)

  # Write CSV
  write.csv(df, path_csv, row.names = FALSE)

  invisible(NULL)
}

#' Convert CSV to TOON
#'
#' @param path_csv Character scalar. Path to input CSV file.
#' @param path_toon Character scalar. Path to output TOON file.
#' @param tabular Logical. If TRUE (default), write as tabular TOON array.
#' @param strict Logical. If TRUE (default), enforce strict syntax on output.
#' @param col_types Named character vector specifying column types.
#'
#' @return Invisibly returns NULL.
#'
#' @examples
#' \dontrun{
#' csv_to_toon("data.csv", "data.toon")
#' }
#'
#' @export
csv_to_toon <- function(path_csv, path_toon, tabular = TRUE, strict = TRUE,
                        col_types = NULL) {
  if (!is.character(path_csv) || length(path_csv) != 1) {
    stop("path_csv must be a single character string")
  }
  if (!is.character(path_toon) || length(path_toon) != 1) {
    stop("path_toon must be a single character string")
  }

  # Read CSV
  df <- read.csv(path_csv, stringsAsFactors = FALSE)

  # Apply column types if specified
  if (!is.null(col_types)) {
    for (name in names(col_types)) {
      if (name %in% names(df)) {
        type <- col_types[[name]]
        df[[name]] <- switch(type,
          "logical" = as.logical(df[[name]]),
          "integer" = as.integer(df[[name]]),
          "double" = as.numeric(df[[name]]),
          "character" = as.character(df[[name]]),
          df[[name]]
        )
      }
    }
  }

  # Write TOON
  write_toon_df(df, path_toon, tabular = tabular, strict = strict)

  invisible(NULL)
}

# Parquet/Feather (optional; requires arrow)

#' Convert TOON to Parquet
#'
#' @param path_toon Character scalar. Path to input TOON file.
#' @param path_parquet Character scalar. Path to output Parquet file.
#' @param key Character scalar or NULL. If non-NULL, extract tabular array.
#' @param strict Logical. If TRUE (default), enforce strict syntax.
#' @param allow_comments Logical. If TRUE (default), allow comments.
#' @param warn Logical. If TRUE (default), emit warnings.
#'
#' @return Invisibly returns NULL.
#'
#' @details
#' Requires the arrow package. If not installed, an error is thrown.
#'
#' @examples
#' \dontrun{
#' toon_to_parquet("data.toon", "data.parquet")
#' }
#'
#' @export
toon_to_parquet <- function(path_toon, path_parquet, key = NULL, strict = TRUE,
                            allow_comments = TRUE, warn = TRUE) {
  if (!requireNamespace("arrow", quietly = TRUE)) {
    stop("Install arrow to use parquet/feather conversion: install.packages('arrow')")
  }

  if (!is.character(path_toon) || length(path_toon) != 1) {
    stop("path_toon must be a single character string")
  }
  if (!is.character(path_parquet) || length(path_parquet) != 1) {
    stop("path_parquet must be a single character string")
  }

  # Read TOON as data.frame
  df <- read_toon_df(path_toon, key = key, strict = strict,
                     allow_comments = allow_comments, warn = warn)

  # Write Parquet
  arrow::write_parquet(df, path_parquet)

  invisible(NULL)
}

#' Convert Parquet to TOON
#'
#' @param path_parquet Character scalar. Path to input Parquet file.
#' @param path_toon Character scalar. Path to output TOON file.
#' @param tabular Logical. If TRUE (default), write as tabular TOON array.
#' @param strict Logical. If TRUE (default), enforce strict syntax on output.
#'
#' @return Invisibly returns NULL.
#'
#' @details
#' Requires the arrow package. If not installed, an error is thrown.
#'
#' @examples
#' \dontrun{
#' parquet_to_toon("data.parquet", "data.toon")
#' }
#'
#' @export
parquet_to_toon <- function(path_parquet, path_toon, tabular = TRUE,
                            strict = TRUE) {
  if (!requireNamespace("arrow", quietly = TRUE)) {
    stop("Install arrow to use parquet/feather conversion: install.packages('arrow')")
  }

  if (!is.character(path_parquet) || length(path_parquet) != 1) {
    stop("path_parquet must be a single character string")
  }
  if (!is.character(path_toon) || length(path_toon) != 1) {
    stop("path_toon must be a single character string")
  }

  # Read Parquet
  df <- as.data.frame(arrow::read_parquet(path_parquet))

  # Write TOON
  write_toon_df(df, path_toon, tabular = tabular, strict = strict)

  invisible(NULL)
}

#' Convert TOON to Feather
#'
#' @param path_toon Character scalar. Path to input TOON file.
#' @param path_feather Character scalar. Path to output Feather file.
#' @param key Character scalar or NULL. If non-NULL, extract tabular array.
#' @param strict Logical. If TRUE (default), enforce strict syntax.
#' @param allow_comments Logical. If TRUE (default), allow comments.
#' @param warn Logical. If TRUE (default), emit warnings.
#'
#' @return Invisibly returns NULL.
#'
#' @details
#' Requires the arrow package. If not installed, an error is thrown.
#'
#' @examples
#' \dontrun{
#' toon_to_feather("data.toon", "data.feather")
#' }
#'
#' @export
toon_to_feather <- function(path_toon, path_feather, key = NULL, strict = TRUE,
                            allow_comments = TRUE, warn = TRUE) {
  if (!requireNamespace("arrow", quietly = TRUE)) {
    stop("Install arrow to use parquet/feather conversion: install.packages('arrow')")
  }

  if (!is.character(path_toon) || length(path_toon) != 1) {
    stop("path_toon must be a single character string")
  }
  if (!is.character(path_feather) || length(path_feather) != 1) {
    stop("path_feather must be a single character string")
  }

  # Read TOON as data.frame
  df <- read_toon_df(path_toon, key = key, strict = strict,
                     allow_comments = allow_comments, warn = warn)

  # Write Feather
  arrow::write_feather(df, path_feather)

  invisible(NULL)
}

#' Convert Feather to TOON
#'
#' @param path_feather Character scalar. Path to input Feather file.
#' @param path_toon Character scalar. Path to output TOON file.
#' @param tabular Logical. If TRUE (default), write as tabular TOON array.
#' @param strict Logical. If TRUE (default), enforce strict syntax on output.
#'
#' @return Invisibly returns NULL.
#'
#' @details
#' Requires the arrow package. If not installed, an error is thrown.
#'
#' @examples
#' \dontrun{
#' feather_to_toon("data.feather", "data.toon")
#' }
#'
#' @export
feather_to_toon <- function(path_feather, path_toon, tabular = TRUE,
                            strict = TRUE) {
  if (!requireNamespace("arrow", quietly = TRUE)) {
    stop("Install arrow to use parquet/feather conversion: install.packages('arrow')")
  }

  if (!is.character(path_feather) || length(path_feather) != 1) {
    stop("path_feather must be a single character string")
  }
  if (!is.character(path_toon) || length(path_toon) != 1) {
    stop("path_toon must be a single character string")
  }

  # Read Feather
  df <- as.data.frame(arrow::read_feather(path_feather))

  # Write TOON
  write_toon_df(df, path_toon, tabular = tabular, strict = strict)

  invisible(NULL)
}
