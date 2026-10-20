# Arguments that never had an effect: deprecated in 0.1.1

test_that("tabular = FALSE warns and is ignored", {
  df <- data.frame(x = 1:2, y = c("a", "b"))
  f_default <- tempfile(fileext = ".toon")
  f_flag <- tempfile(fileext = ".toon")
  on.exit(unlink(c(f_default, f_flag)))

  expect_silent(write_toon_df(df, f_default))
  expect_warning(write_toon_df(df, f_flag, tabular = FALSE),
                 "`tabular` is deprecated and ignored")
  expect_identical(readLines(f_flag), readLines(f_default))
})

test_that("csv_to_toon(tabular = FALSE) warns once", {
  csv <- tempfile(fileext = ".csv")
  out <- tempfile(fileext = ".toon")
  on.exit(unlink(c(csv, out)))
  write.csv(data.frame(x = 1:2), csv, row.names = FALSE)

  expect_silent(csv_to_toon(csv, out))
  warnings <- testthat::capture_warnings(csv_to_toon(csv, out, tabular = FALSE))
  expect_length(warnings, 1)
  expect_match(warnings, "`tabular` is deprecated")
})

test_that("toon_stream_write_rows(batch_size =) warns and is ignored", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  make_source <- function() {
    done <- FALSE
    function() {
      if (done) return(NULL)
      done <<- TRUE
      data.frame(x = 1:3)
    }
  }

  expect_silent(toon_stream_write_rows(tmp, "x", make_source()))
  expect_warning(
    n <- toon_stream_write_rows(tmp, "x", make_source(), batch_size = 1L),
    "`batch_size` is deprecated and ignored"
  )
  expect_equal(n, 3)
})

test_that("allow_duplicate_keys = FALSE warns in tabular readers", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines("[2]{x}:\n  1\n  2", tmp)

  expect_silent(read_toon_df(tmp))
  expect_warning(read_toon_df(tmp, allow_duplicate_keys = FALSE),
                 "`allow_duplicate_keys` is deprecated and ignored")
  expect_warning(
    toon_stream_rows(tmp, callback = function(b) NULL, allow_duplicate_keys = FALSE),
    "`allow_duplicate_keys` is deprecated and ignored"
  )
})

test_that("string argument checks keep their messages and calls", {
  err <- tryCatch(read_toon(1), error = identity)
  expect_identical(conditionMessage(err), "file must be a single character string")
  expect_identical(conditionCall(err)[[1]], as.name("read_toon"))

  expect_error(toon_to_csv("a.toon", c("x", "y")),
               "path_csv must be a single character string")
  expect_error(validate_toon(NA), "x must be a single character string")
})
