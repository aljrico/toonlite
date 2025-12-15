# Tests for primitive parsing

test_that("null is parsed correctly", {
  expect_null(from_toon("null"))
})

test_that("booleans are parsed correctly", {
  expect_true(from_toon("true"))
  expect_false(from_toon("false"))
})

test_that("integers are parsed correctly", {
  expect_equal(from_toon("0"), 0L)
  expect_equal(from_toon("42"), 42L)
  expect_equal(from_toon("-123"), -123L)
  expect_equal(from_toon("2147483647"), 2147483647L)  # INT32_MAX
  # INT32_MIN (-2147483648) is stored as double because it equals R's NA_integer_
  expect_equal(from_toon("-2147483648"), -2147483648)  # INT32_MIN (as double)
})

test_that("doubles are parsed correctly", {
  expect_equal(from_toon("3.14"), 3.14)
  expect_equal(from_toon("-2.5"), -2.5)
  expect_equal(from_toon("1e10"), 1e10)
  expect_equal(from_toon("1.5e-3"), 1.5e-3)
})

test_that("strings are parsed correctly", {
  expect_equal(from_toon('"hello"'), "hello")
  expect_equal(from_toon('"hello world"'), "hello world")
  expect_equal(from_toon('""'), "")
})

test_that("string escapes are handled", {
  expect_equal(from_toon('"line1\\nline2"'), "line1\nline2")
  expect_equal(from_toon('"tab\\there"'), "tab\there")
  expect_equal(from_toon('"quote\\"inside"'), 'quote"inside')
  expect_equal(from_toon('"back\\\\slash"'), "back\\slash")
})

test_that("unicode escapes work", {
  expect_equal(from_toon('"\\u0041"'), "A")
  expect_equal(from_toon('"\\u00e9"'), "\u00e9")  # é
})

test_that("type inference promotes correctly", {
  # Logical to integer
  toon <- "value:\n  - true\n  - 1"
  result <- from_toon(toon)
  # Should be promoted based on content
  expect_true(is.list(result))
})
