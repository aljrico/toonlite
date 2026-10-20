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
