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

test_that("read_toon_df decodes quoted strings and rejects invalid escapes", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines('[2]{s}:\n  "caf\\u00e9"\n  "\\u0041\\t"', tmp)
  expect_identical(read_toon_df(tmp)$s, c("caf\u00e9", "A\t"))

  # Invalid escapes are errors in both modes (spec section 14.2)
  writeLines('[1]{s}:\n  "bad\\qesc"', tmp)
  expect_error(read_toon_df(tmp, strict = TRUE), "Invalid escape")
  expect_error(read_toon_df(tmp, strict = FALSE), "Invalid escape")
})

write_lines_tmp <- function(lines) {
  tmp <- tempfile(fileext = ".toon")
  writeLines(lines, tmp)
  tmp
}

test_that("read_toon_df follows the spec by default for width and count", {
  tmp <- write_lines_tmp(c("[2]{a,b}:", "  1,2", "  3"))
  on.exit(unlink(tmp))
  expect_error(read_toon_df(tmp), "1 value but the header declares 2 fields")

  writeLines(c("[3]{a,b}:", "  1,2", "  3,4"), tmp)
  expect_error(read_toon_df(tmp), "Declared 3 rows but found 2")
  # Non-strict decoding accepts the rows found (spec recovery), with a warning
  expect_warning(df <- read_toon_df(tmp, strict = FALSE), "Declared 3 rows")
  expect_identical(df$a, c(1L, 3L))
  expect_silent(read_toon_df(tmp, strict = FALSE, warn = FALSE))
})

test_that("read_toon_df reads root[key] with the v4 header syntax", {
  tmp <- write_lines_tmp(c(
    "meta:", "  n: 2",
    "users[2]{id,name}:", "  1,Alice", "  2,\"Bob, Jr.\"",
    "after: true"
  ))
  on.exit(unlink(tmp))
  df <- read_toon_df(tmp, key = "users")
  expect_identical(df, data.frame(id = 1:2, name = c("Alice", "Bob, Jr.")))
  expect_error(read_toon_df(tmp, key = "nope"), "Key not found: nope")
  expect_error(read_toon_df(tmp, key = "meta"), "not a tabular array")
  expect_error(read_toon_df(tmp), "root is not a tabular array")
})

test_that("read_toon_df handles delimiters, nested groups and comments", {
  tmp <- write_lines_tmp(c(
    "# orders",
    "[2|]{id|customer{name|tier}}:",
    "  1|Ann|gold",
    "  # a comment between rows does not end them",
    "  2|\"B|b\"|null"
  ))
  on.exit(unlink(tmp))
  df <- read_toon_df(tmp)
  expect_identical(names(df), c("id", "customer.name", "customer.tier"))
  expect_identical(df$customer.name, c("Ann", "B|b"))
  expect_identical(df$customer.tier, c("gold", NA))
})

test_that("read_toon_df reads empty tables and rejects trailing content", {
  tmp <- write_lines_tmp("[]")
  on.exit(unlink(tmp))
  expect_identical(dim(read_toon_df(tmp)), c(0L, 0L))

  writeLines(c("rows: []"), tmp)
  expect_identical(nrow(read_toon_df(tmp, key = "rows")), 0L)

  writeLines(c("[1]{a}:", "  1", "b: 2"), tmp)
  expect_error(read_toon_df(tmp), "Unexpected content after the root array")
})

test_that("read_toon_df honours indent and canonicalises numbers in text columns", {
  tmp <- write_lines_tmp(c("[3]{x}:", "    1", "    2.50", "    abc"))
  on.exit(unlink(tmp))
  expect_error(read_toon_df(tmp, indent = 4L, strict = TRUE), NA)
  expect_error(read_toon_df(tmp), "Indentation jumps")
  expect_identical(read_toon_df(tmp, indent = 4L)$x, c("1", "2.5", "abc"))
  expect_identical(read_toon_df(tmp, indent = 4L, col_types = c(x = "character"))$x,
                   c("1", "2.50", "abc"))
  expect_error(read_toon_df(tmp, indent = 0), "positive whole number")
})

test_that("read_toon_df keeps doubles exact when a column turns character", {
  tmp <- write_lines_tmp(c("[3]{x}:", "  0.1", "  123456789.123", "  z"))
  on.exit(unlink(tmp))
  expect_identical(read_toon_df(tmp)$x, c("0.1", "123456789.123", "z"))
})

test_that("allow_comments is deprecated in the tabular readers", {
  tmp <- write_lines_tmp(c("[1]{a}:", "  1"))
  on.exit(unlink(tmp))
  expect_warning(read_toon_df(tmp, allow_comments = FALSE), "allow_comments")
  expect_warning(toon_stream_rows(tmp, callback = identity, allow_comments = FALSE),
                 "allow_comments")
})
