# Tests for tabular array parsing

test_that("basic tabular array is parsed to data.frame", {
  toon <- "[3]{name,age,active}:\n  \"Alice\", 30, true\n  \"Bob\", 25, false\n  \"Charlie\", 35, true"

  # Write to temp file
  tmp <- tempfile(fileext = ".toon")
  writeLines(toon, tmp)

  result <- read_toon_df(tmp)

  expect_true(is.data.frame(result))
  expect_equal(nrow(result), 3)
  expect_equal(ncol(result), 3)
  expect_equal(names(result), c("name", "age", "active"))
  expect_equal(result$name, c("Alice", "Bob", "Charlie"))
  expect_equal(result$age, c(30L, 25L, 35L))
  expect_equal(result$active, c(TRUE, FALSE, TRUE))

  unlink(tmp)
})

test_that("tabular array with null values works", {
  toon <- "[3]{a,b}:\n  1, 2\n  null, 4\n  5, null"

  tmp <- tempfile(fileext = ".toon")
  writeLines(toon, tmp)

  result <- read_toon_df(tmp)

  expect_true(is.na(result$a[2]))
  expect_true(is.na(result$b[3]))
  expect_equal(result$a[1], 1L)

  unlink(tmp)
})

test_that("ragged rows expand_warn fills missing with NA", {
  toon <- "[3]{a,b,c}:\n  1, 2, 3\n  4, 5\n  6, 7, 8"

  tmp <- tempfile(fileext = ".toon")
  writeLines(toon, tmp)

  expect_warning(
    result <- read_toon_df(tmp, ragged_rows = "expand_warn"),
    "inconsistent field counts"
  )

  expect_equal(nrow(result), 3)
  expect_true(is.na(result$c[2]))

  unlink(tmp)
})

test_that("ragged rows expand_warn expands schema", {
  toon <- "[3]{a,b}:\n  1, 2\n  3, 4, 5\n  6, 7"

  tmp <- tempfile(fileext = ".toon")
  writeLines(toon, tmp)

  expect_warning(
    result <- read_toon_df(tmp, ragged_rows = "expand_warn"),
    "inconsistent"
  )

  expect_equal(ncol(result), 3)  # Schema expanded
  expect_true(is.na(result[[3]][1]))  # Backfilled with NA

  unlink(tmp)
})

test_that("ragged_rows error rejects mismatched rows", {
  toon <- "[2]{a,b}:\n  1, 2\n  3, 4, 5"

  tmp <- tempfile(fileext = ".toon")
  writeLines(toon, tmp)

  expect_error(
    read_toon_df(tmp, ragged_rows = "error"),
    "fields"
  )

  unlink(tmp)
})

test_that("n_mismatch warn accepts count mismatch", {
  toon <- "[5]{a,b}:\n  1, 2\n  3, 4"

  tmp <- tempfile(fileext = ".toon")
  writeLines(toon, tmp)

  expect_warning(
    result <- read_toon_df(tmp, n_mismatch = "warn"),
    "Declared"
  )

  expect_equal(nrow(result), 2)

  unlink(tmp)
})

test_that("n_mismatch error rejects count mismatch", {
  toon <- "[5]{a,b}:\n  1, 2\n  3, 4"

  tmp <- tempfile(fileext = ".toon")
  writeLines(toon, tmp)

  expect_error(
    read_toon_df(tmp, n_mismatch = "error"),
    "Declared"
  )

  unlink(tmp)
})

test_that("col_types forces column types", {
  toon <- "[2]{x,y}:\n  1, 2\n  3, 4"

  tmp <- tempfile(fileext = ".toon")
  writeLines(toon, tmp)

  result <- read_toon_df(tmp, col_types = c(x = "double", y = "character"))

  expect_true(is.double(result$x))
  expect_true(is.character(result$y))

  unlink(tmp)
})

test_that("type inference promotes correctly", {
  toon <- "[3]{val}:\n  1\n  2.5\n  3"

  tmp <- tempfile(fileext = ".toon")
  writeLines(toon, tmp)

  result <- read_toon_df(tmp)

  # Should be promoted to double
  expect_true(is.double(result$val))

  unlink(tmp)
})

test_that("read_toon_df reads INT32_MIN as double, not NA", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines("[2]{x}:\n  -2147483648\n  5", tmp)

  result <- read_toon_df(tmp)

  expect_type(result$x, "double")
  expect_identical(result$x, c(-2147483648, 5))
  # Same rule as the DOM parser
  expect_identical(from_toon("x: -2147483648")$x, -2147483648)
})

test_that("read_toon_df decodes quoted strings exactly like read_toon", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines('[3]{s}:\n  "caf\\u00e9"\n  "\\u0041\\t"\n  "bad\\qesc"', tmp)

  for (strict in c(TRUE, FALSE)) {
    df <- read_toon_df(tmp, strict = strict)
    dom <- vapply(read_toon(tmp, strict = strict), function(r) r$s, character(1))
    expect_identical(df$s, dom)
  }

  expect_identical(read_toon_df(tmp)$s[1:2], c("café", "A\t"))
  # Invalid escape: strict keeps the raw literal, permissive decodes leniently
  expect_identical(read_toon_df(tmp, strict = TRUE)$s[3], '"bad\\qesc"')
  expect_identical(read_toon_df(tmp, strict = FALSE)$s[3], "bad\\qesc")
})
