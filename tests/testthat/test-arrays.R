# Tests for array parsing

test_that("list array is parsed correctly", {
  toon <- "items:\n  - 1\n  - 2\n  - 3"
  result <- from_toon(toon)

  expect_true(is.list(result))
  expect_equal(result$items, c(1L, 2L, 3L))
})

test_that("array header with count is parsed", {
  toon <- "[3]:\n  - \"a\"\n  - \"b\"\n  - \"c\""
  result <- from_toon(toon)

  expect_equal(result, c("a", "b", "c"))
})

test_that("nested arrays work", {
  toon <- "matrix:\n  - - 1\n    - 2\n  - - 3\n    - 4"
  result <- from_toon(toon, simplify = FALSE)

  expect_true(is.list(result))
  expect_true(is.list(result$matrix))
})

test_that("mixed type array is kept as list when simplify=FALSE", {
  toon <- "data:\n  - 1\n  - \"two\"\n  - true"
  result <- from_toon(toon, simplify = FALSE)

  expect_true(is.list(result$data))
  expect_equal(length(result$data), 3)
})

test_that("homogeneous array simplifies to vector", {
  toon <- "numbers:\n  - 1\n  - 2\n  - 3"
  result <- from_toon(toon, simplify = TRUE)

  expect_true(is.integer(result$numbers))
  expect_equal(result$numbers, c(1L, 2L, 3L))
})

test_that("array with null simplifies correctly", {
  toon <- "values:\n  - 1\n  - null\n  - 3"
  result <- from_toon(toon, simplify = TRUE)

  expect_true(is.integer(result$values))
  expect_true(is.na(result$values[2]))
})

test_that("count mismatch warns by default", {
  toon <- "[5]:\n  - 1\n  - 2\n  - 3"

  expect_warning(
    result <- from_toon(toon),
    "observed"
  )
  expect_equal(length(result), 3)
})
