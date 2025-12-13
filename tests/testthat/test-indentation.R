# Tests for indentation handling

test_that("spaces-only indentation works", {
  toon <- "outer:\n  inner: 42"
  result <- from_toon(toon)

  expect_equal(result$outer$inner, 42L)
})

test_that("tabs in strict mode cause error", {
  toon <- "outer:\n\tinner: 42"

  expect_error(
    from_toon(toon, strict = TRUE),
    "Tab"
  )
})

test_that("mixed indentation levels work", {
  toon <- "level0:\n  level1:\n      level2: 42"
  result <- from_toon(toon)

  expect_equal(result$level0$level1$level2, 42L)
})

test_that("dedent returns to parent level", {
  toon <- "first:\n  a: 1\n  b: 2\nsecond:\n  c: 3"
  result <- from_toon(toon)

  expect_equal(result$first$a, 1L)
  expect_equal(result$first$b, 2L)
  expect_equal(result$second$c, 3L)
})

test_that("CRLF line endings work", {
  toon <- "key: 42\r\nother: 24"
  result <- from_toon(toon)

  expect_equal(result$key, 42L)
  expect_equal(result$other, 24L)
})
