# Internal helpers (not exported)

# Error unless `x` is a single character string. The message and the reported
# call (the user-facing function) match the inline checks this replaces.
check_string <- function(x, arg = deparse(substitute(x))) {
  if (!is.character(x) || length(x) != 1) {
    stop(simpleError(paste0(arg, " must be a single character string"),
                     sys.call(-1)))
  }
  invisible(x)
}

# `tabular` never changed the output: data frames are always written as
# tabular TOON arrays. Warn when a caller asks for anything else.
warn_tabular_deprecated <- function(tabular) {
  if (!isTRUE(tabular)) {
    warning(simpleWarning(
      paste("`tabular` is deprecated and ignored: data frames are always",
            "written as tabular TOON arrays."),
      sys.call(-1)
    ))
  }
  invisible(NULL)
}

# `allow_duplicate_keys` is never consulted by the tabular readers.
warn_duplicate_keys_deprecated <- function(allow_duplicate_keys) {
  if (!isTRUE(allow_duplicate_keys)) {
    warning(simpleWarning(
      paste("`allow_duplicate_keys` is deprecated and ignored: it has no",
            "effect on tabular reads."),
      sys.call(-1)
    ))
  }
  invisible(NULL)
}

# The DOM decoders used to accept duplicate keys by default. The spec makes
# them an error in strict mode and last-write-wins in non-strict mode.
warn_decoder_duplicate_keys_deprecated <- function() {
  warning(simpleWarning(
    paste("`allow_duplicate_keys` is deprecated and ignored: duplicate keys",
          "are an error when `strict = TRUE` and the last one wins when",
          "`strict = FALSE`."),
    sys.call(-1)
  ))
  invisible(NULL)
}

# Full-line `#` comments are part of the TOON spec (section 5.1) and are always
# skipped; `//` and trailing comments are not TOON syntax. Rejecting comment
# lines would be non-conformant, so `allow_comments = FALSE` is ignored.
warn_comments_deprecated <- function(allow_comments) {
  if (!isTRUE(allow_comments)) {
    warning(simpleWarning(
      paste("`allow_comments` is deprecated and ignored: full-line `#`",
            "comments are part of TOON and are always skipped."),
      sys.call(-1)
    ))
  }
  invisible(NULL)
}

# Spaces per indentation level (the spec's indentSize): a positive integer.
check_indent <- function(indent, arg = "indent") {
  if (!is.numeric(indent) || length(indent) != 1 || is.na(indent) ||
      indent < 1 || indent != trunc(indent)) {
    stop(simpleError(paste0("`", arg, "` must be a positive whole number"),
                     sys.call(-1)))
  }
  as.integer(indent)
}

check_col_types <- function(col_types) {
  if (!is.null(col_types)) {
    if (!is.character(col_types) || is.null(names(col_types))) {
      stop(simpleError("col_types must be a named character vector",
                       sys.call(-1)))
    }
    bad <- setdiff(col_types, c("logical", "integer", "double", "character"))
    if (length(bad) > 0) {
      stop(simpleError(
        paste0("Unknown col_types: ", paste(unique(bad), collapse = ", "),
               ". Use \"logical\", \"integer\", \"double\" or \"character\"."),
        sys.call(-1)))
    }
  }
  invisible(col_types)
}

# `pretty = FALSE` produced invalid TOON (rows glued together); output is
# always the canonical multi-line form now.
warn_pretty_deprecated <- function(pretty) {
  if (!isTRUE(pretty)) {
    warning(simpleWarning(
      paste("`pretty` is deprecated and ignored: TOON output is always",
            "multi-line (`pretty = FALSE` produced invalid TOON)."),
      sys.call(-1)
    ))
  }
  invisible(NULL)
}

# The encoders' `strict` used to reject NaN/Inf. The spec maps them to null.
warn_encoder_strict_deprecated <- function() {
  warning(simpleWarning(
    paste("`strict` is deprecated and ignored when writing: NaN and Inf",
          "are always written as null (TOON spec section 3)."),
    sys.call(-1)
  ))
  invisible(NULL)
}

# Document delimiter: ",", "\t" or "|" (or the names comma, tab, pipe).
check_delimiter <- function(delimiter) {
  choices <- c(comma = ",", tab = "\t", pipe = "|")
  if (is.character(delimiter) && length(delimiter) == 1 && !is.na(delimiter)) {
    if (delimiter %in% choices) return(delimiter)
    if (delimiter %in% names(choices)) return(unname(choices[delimiter]))
  }
  stop(simpleError(
    "`delimiter` must be \",\", \"\\t\" or \"|\" (or \"comma\", \"tab\", \"pipe\")",
    sys.call(-1)))
}
