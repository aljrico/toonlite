# Tests for TOON encoding/serialization

test_that("null encodes correctly", {
  result <- to_toon(NULL)
  expect_match(result, "null")
})

test_that("booleans encode correctly", {
  expect_match(to_toon(TRUE), "true")
  expect_match(to_toon(FALSE), "false")
})

test_that("integers encode correctly", {
  expect_match(to_toon(42L), "42")
  expect_match(to_toon(-123L), "-123")
})

test_that("doubles encode correctly", {
  result <- to_toon(3.14)
  expect_true(grepl("3.14", result))
})

test_that("strings encode with quotes", {
  result <- to_toon("hello")
  expect_match(result, '"hello"')
})

test_that("string escapes are encoded", {
  result <- to_toon("line1\nline2")
  expect_true(grepl("\\\\n", result))  # Should contain \n escape
})

test_that("NA values encode as null", {
  result <- to_toon(NA)
  expect_match(result, "null")
})

test_that("integer NA encodes as null", {
  result <- to_toon(NA_integer_)
  expect_match(result, "null")
})

test_that("named list encodes as object", {
  result <- to_toon(list(a = 1, b = 2))
  expect_true(grepl("a:", result))
  expect_true(grepl("b:", result))
})

test_that("unnamed list encodes as array", {
  result <- to_toon(list(1, 2, 3))
  expect_true(grepl("\\[3\\]", result))
})

test_that("vector encodes as array", {
  result <- to_toon(c(1L, 2L, 3L))
  expect_true(grepl("\\[3\\]", result))
})

test_that("data.frame encodes as tabular array", {
  df <- data.frame(a = 1:2, b = c("x", "y"), stringsAsFactors = FALSE)
  result <- to_toon(df)

  expect_true(grepl("\\{a,b\\}", result))
})

test_that("factor encodes as character", {
  result <- to_toon(factor(c("a", "b", "a")))
  expect_true(grepl('"a"', result))
  expect_true(grepl('"b"', result))
})

test_that("Date encodes as ISO string", {
  d <- as.Date("2024-01-15")
  result <- to_toon(d)
  expect_true(grepl("2024-01-15", result))
})

test_that("POSIXct encodes as ISO datetime string", {
  dt <- as.POSIXct("2024-01-15 10:30:00", tz = "UTC")
  result <- to_toon(dt)
  expect_true(grepl("2024-01-15", result))
  expect_true(grepl("10:30:00", result))
})

test_that("NaN rejected in strict mode", {
  expect_error(to_toon(NaN, strict = TRUE), "NaN")
})

test_that("Inf rejected in strict mode", {
  expect_error(to_toon(Inf, strict = TRUE), "Inf")
})

test_that("pretty=FALSE produces compact output", {
  result <- to_toon(list(a = 1, b = 2), pretty = FALSE)
  # Compact output should have fewer lines
  expect_true(length(strsplit(result, "\n")[[1]]) <= 3)
})

test_that("indent parameter works", {
  result <- to_toon(list(a = list(b = 1)), indent = 4L)
  # Should have 4-space indentation
  expect_true(grepl("    b:", result))
})
