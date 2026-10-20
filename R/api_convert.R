#' @title TOON Conversion Functions
#' @name conversions
#' @description Functions for converting between TOON and other formats.
NULL

# JSON <-> TOON (lossless lane)

#' Convert JSON to TOON
#'
#' @param json Character scalar containing JSON.
#' @param pretty Deprecated and ignored (see [to_toon()]).
#' @param strict Deprecated and ignored (see [to_toon()]).
#' @param allow_comments Logical. Currently has no effect: JSON input cannot
#'   contain comments and the encoder never writes them.
#' @param delimiter Document delimiter: `","` (default), `"\t"` or `"|"`.
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
                         allow_comments = TRUE, delimiter = ",") {
  if (!requireNamespace("jsonlite", quietly = TRUE)) {
    stop("Install jsonlite to use JSON conversion: install.packages('jsonlite')")
  }

  check_string(json)

  # Parse JSON to R object
  obj <- jsonlite::fromJSON(json, simplifyVector = FALSE)

  warn_pretty_deprecated(pretty)
  if (!missing(strict)) warn_encoder_strict_deprecated()

  to_toon(obj, delimiter = delimiter)
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

  check_string(toon)

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
  check_string(path_toon)
  check_string(path_csv)

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
#' @param tabular Deprecated and ignored. Data frames are always written as a
#'   tabular TOON array; passing anything other than TRUE gives a warning.
#' @param strict Deprecated and ignored (see [to_toon()]).
#' @param delimiter Document delimiter: `","` (default), `"\t"` or `"|"`.
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
                        col_types = NULL, delimiter = ",") {
  check_string(path_csv)
  check_string(path_toon)
  warn_tabular_deprecated(tabular)
  if (!missing(strict)) warn_encoder_strict_deprecated()

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
  write_toon_df(df, path_toon, delimiter = delimiter)

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

  check_string(path_toon)
  check_string(path_parquet)

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
#' @param tabular Deprecated and ignored. Data frames are always written as a
#'   tabular TOON array; passing anything other than TRUE gives a warning.
#' @param strict Deprecated and ignored (see [to_toon()]).
#' @param delimiter Document delimiter: `","` (default), `"\t"` or `"|"`.
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
                            strict = TRUE, delimiter = ",") {
  if (!requireNamespace("arrow", quietly = TRUE)) {
    stop("Install arrow to use parquet/feather conversion: install.packages('arrow')")
  }

  check_string(path_parquet)
  check_string(path_toon)
  warn_tabular_deprecated(tabular)
  if (!missing(strict)) warn_encoder_strict_deprecated()

  # Read Parquet
  df <- as.data.frame(arrow::read_parquet(path_parquet))

  # Write TOON
  write_toon_df(df, path_toon, delimiter = delimiter)

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

  check_string(path_toon)
  check_string(path_feather)

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
#' @param tabular Deprecated and ignored. Data frames are always written as a
#'   tabular TOON array; passing anything other than TRUE gives a warning.
#' @param strict Deprecated and ignored (see [to_toon()]).
#' @param delimiter Document delimiter: `","` (default), `"\t"` or `"|"`.
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
                            strict = TRUE, delimiter = ",") {
  if (!requireNamespace("arrow", quietly = TRUE)) {
    stop("Install arrow to use parquet/feather conversion: install.packages('arrow')")
  }

  check_string(path_feather)
  check_string(path_toon)
  warn_tabular_deprecated(tabular)
  if (!missing(strict)) warn_encoder_strict_deprecated()

  # Read Feather
  df <- as.data.frame(arrow::read_feather(path_feather))

  # Write TOON
  write_toon_df(df, path_toon, delimiter = delimiter)

  invisible(NULL)
}
