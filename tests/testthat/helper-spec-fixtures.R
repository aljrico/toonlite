# Helpers for the official TOON conformance fixtures.
#
# The fixtures in fixtures/spec-v4.4.0/ are copied unmodified from
# https://github.com/toon-format/spec (tag v4.4.0, commit
# d09c3d33e37389345a74bd96981eb96dd9be6c5e), MIT licensed; see the LICENSE
# file next to them.
#
# JSON <-> R mapping used by the runner. Fixture files are read with
# jsonlite::read_json(simplifyVector = FALSE), so every JSON array stays a list
# and objects and arrays never collapse into atomic vectors:
#
#   JSON object  <-> named list (names attribute present; {} is
#                    setNames(list(), character(0)))
#   JSON array   <-> unnamed list ([] is list())
#   JSON string  <-> character(1)
#   JSON number  <-> integer(1) or double(1), compared by value as doubles
#                    (JSON-model equality, spec section 2)
#   true / false <-> logical(1)
#   null         <-> NULL
#
# Encode tests pass the decoded `input` to to_toon() and compare the TOON text
# byte for byte. Decode tests call from_toon(simplify = FALSE) and compare the
# result with `expected` using json_equal(), which checks key order too.

spec_fixture_dir <- function() {
  testthat::test_path("fixtures", "spec-v4.4.0")
}

spec_fixture_files <- function(category) {
  sort(list.files(file.path(spec_fixture_dir(), category),
                  pattern = "\\.json$", full.names = TRUE))
}

read_spec_fixture <- function(path) {
  jsonlite::read_json(path, simplifyVector = FALSE)
}

# Fixtures that cannot pass because of R's data model, keyed by
# "<category>/<file>#<index>" (zero-based index into the file's `tests`).
# Each entry is the reason given to testthat::skip(). Keep this list short.
spec_known_skips <- list()

spec_version_ok <- function(min_version, target = "4.4") {
  if (is.null(min_version)) {
    return(TRUE)
  }
  package_version(min_version) <= package_version(target)
}

# Recursive JSON-model equality between two R values in the mapping above.
json_equal <- function(a, b) {
  if (is.null(a) || is.null(b)) {
    return(is.null(a) && is.null(b))
  }
  if (is.list(a) || is.list(b)) {
    if (!is.list(a) || !is.list(b) || length(a) != length(b)) {
      return(FALSE)
    }
    na <- names(a)
    nb <- names(b)
    if (is.null(na) != is.null(nb)) {
      return(FALSE)
    }
    if (!is.null(na) && !identical(enc2utf8(na), enc2utf8(nb))) {
      return(FALSE)
    }
    for (i in seq_along(a)) {
      if (!json_equal(a[[i]], b[[i]])) {
        return(FALSE)
      }
    }
    return(TRUE)
  }
  if (length(a) != 1L || length(b) != 1L) {
    return(FALSE)
  }
  if (is.numeric(a) && is.numeric(b)) {
    return(isTRUE(as.double(a) == as.double(b)))
  }
  if (is.logical(a) && is.logical(b)) {
    return(identical(a, b))
  }
  if (is.character(a) && is.character(b)) {
    return(identical(enc2utf8(a), enc2utf8(b)))
  }
  FALSE
}

json_show <- function(x) {
  as.character(jsonlite::toJSON(x, auto_unbox = TRUE, null = "null",
                                digits = NA))
}

# Map fixture options to toonlite arguments.
spec_encode_args <- function(options) {
  args <- list()
  if (!is.null(options$delimiter)) args$delimiter <- options$delimiter
  if (!is.null(options$indentSize)) args$indent <- options$indentSize
  args
}

spec_decode_args <- function(options) {
  args <- list(simplify = FALSE)
  if (!is.null(options$strict)) args$strict <- options$strict
  if (!is.null(options$indentSize)) args$indent <- options$indentSize
  args
}
