# Official TOON v4.4.0 conformance fixtures (see helper-spec-fixtures.R).
#
# Every test case runs as its own test, named "<category>/<file>#<index>" after
# the fixture file and its zero-based position in `tests`, never after the
# prose `name`, which upstream rewrites freely.

skip_if_not_installed("jsonlite")

run_encode_case <- function(tc) {
  args <- spec_encode_args(tc$options)
  if (isTRUE(tc$shouldError)) {
    expect_error(do.call(to_toon, c(list(tc$input), args)))
    return(invisible())
  }
  out <- do.call(to_toon, c(list(tc$input), args))
  expect_identical(as.vector(unclass(out)), tc$expected,
                   label = paste0("to_toon() for '", tc$name, "'"))
}

run_decode_case <- function(tc) {
  args <- spec_decode_args(tc$options)
  if (isTRUE(tc$shouldError)) {
    expect_error(do.call(from_toon, c(list(tc$input), args)),
                 info = tc$name)
    return(invisible())
  }
  # Non-strict fixtures accept count mismatches; toonlite also warns.
  out <- withCallingHandlers(
    do.call(from_toon, c(list(tc$input), args)),
    warning = function(w) {
      if (isFALSE(args$strict) && grepl("Declared ", conditionMessage(w))) {
        invokeRestart("muffleWarning")
      }
    }
  )
  ok <- json_equal(out, tc$expected)
  expect_true(ok, label = paste0(
    "from_toon() for '", tc$name, "'\n  got:      ", json_show(out),
    "\n  expected: ", json_show(tc$expected)
  ))
}

for (category in c("encode", "decode")) {
  for (path in spec_fixture_files(category)) {
    fixture <- read_spec_fixture(path)
    for (i in seq_along(fixture$tests)) {
      local({
        tc <- fixture$tests[[i]]
        id <- sprintf("%s/%s#%d", category, basename(path), i - 1L)
        runner <- if (category == "encode") run_encode_case else run_decode_case
        test_that(id, {
          if (!spec_version_ok(tc$minSpecVersion)) {
            skip(paste("requires spec", tc$minSpecVersion))
          }
          reason <- spec_known_skips[[id]]
          if (!is.null(reason)) {
            skip(reason)
          }
          runner(tc)
        })
      })
    }
  }
}
