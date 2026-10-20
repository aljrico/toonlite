# Indentation (spec 12, 14.4)

test_that("nested objects follow indentation", {
  expect_identical(from_toon("first:\n  a: 1\n  b: 2\nsecond:\n  c: 3"),
                   list(first = list(a = 1L, b = 2L), second = list(c = 3L)))
})

test_that("tabs and odd indentation are errors in strict mode only", {
  expect_error(from_toon("outer:\n\tinner: 42"), "Tab")
  expect_error(from_toon("outer:\n   inner: 42"), "not a multiple")
  expect_identical(from_toon("outer:\n\tinner: 42", strict = FALSE),
                   list(outer = list(inner = 42L)))
  expect_identical(from_toon("outer:\n   inner: 42", strict = FALSE),
                   list(outer = list(inner = 42L)))
})

test_that("depth jumps error in strict mode and are adopted otherwise", {
  toon <- "level0:\n  level1:\n      level2: 42\n      more: 1"
  expect_error(from_toon(toon), "jumps more than one level")
  expect_identical(from_toon(toon, strict = FALSE),
                   list(level0 = list(level1 = list(level2 = 42L, more = 1L))))
})

test_that("over-indented lines are errors in both modes", {
  expect_error(from_toon("a: 1\n  b: 2"), "Unexpected indentation")
  expect_error(from_toon("a: 1\n  b: 2", strict = FALSE), "Unexpected indentation")
  expect_error(from_toon("  a: 1"), "must not be indented")
})

test_that("indent sets the spaces per level", {
  expect_identical(from_toon("a:\n    b: 1", indent = 4L), list(a = list(b = 1L)))
  expect_error(from_toon("a:\n  b: 1", indent = 4L), "not a multiple")
  expect_error(from_toon("a: 1", indent = 0), "positive whole number")
})

test_that("CRLF line endings, a BOM and a trailing newline are accepted", {
  expect_identical(from_toon("key: 42\r\nother: 24\r\n"), list(key = 42L, other = 24L))
  expect_identical(from_toon("\ufeffkey: 1"), list(key = 1L))
})

test_that("blank lines inside arrays are errors in strict mode only", {
  toon <- "x[2]:\n  - 1\n\n  - 2"
  expect_error(from_toon(toon), "Blank lines")
  expect_identical(from_toon(toon, strict = FALSE), list(x = 1:2))
  expect_identical(from_toon("a: 1\n\nb: 2"), list(a = 1L, b = 2L))
})
