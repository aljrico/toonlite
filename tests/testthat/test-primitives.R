# Primitive decoding (spec 4)

test_that("literals", {
  expect_null(from_toon("null"))
  expect_true(from_toon("true"))
  expect_false(from_toon("false"))
  expect_identical(from_toon("True"), "True")
})

test_that("integers within R's integer range are integer", {
  expect_identical(from_toon("0"), 0L)
  expect_identical(from_toon("-0"), 0L)
  expect_identical(from_toon("42"), 42L)
  expect_identical(from_toon("2147483647"), 2147483647L)
  expect_identical(from_toon("-2147483647"), -2147483647L)
  # -2147483648 is NA_integer_ in R, so it and larger values are doubles
  expect_identical(from_toon("-2147483648"), -2147483648)
  expect_identical(from_toon("2147483648"), 2147483648)
  expect_identical(from_toon("9223372036854775808"), 2^63)
})

test_that("doubles, including out-of-range values", {
  expect_identical(from_toon("3.14"), 3.14)
  expect_identical(from_toon("1e10"), 1e10)
  expect_identical(from_toon("1.5E-3"), 1.5e-3)
  expect_identical(from_toon("1.0"), 1)
  expect_identical(from_toon("1e400"), Inf)
  expect_identical(from_toon("-1e400"), -Inf)
  expect_identical(from_toon("1e-400"), 0)
})

test_that("tokens outside the JSON number grammar are strings", {
  for (tok in c("05", "-05", ".5", "1.", "+5", "1e", "0x10", "Infinity", "NaN", "1_000")) {
    expect_identical(from_toon(tok), tok, info = tok)
  }
})

test_that("quoted strings and escapes", {
  expect_identical(from_toon('"42"'), "42")
  expect_identical(from_toon('""'), "")
  expect_identical(from_toon('"line1\\nline2\\ttab\\r"'), "line1\nline2\ttab\r")
  expect_identical(from_toon('"q\\"b\\\\"'), 'q"b\\')
  expect_identical(from_toon('"\\u0041\\u00E9"'), "A\u00e9")
  expect_identical(from_toon("caf\u00e9"), "caf\u00e9")
  expect_error(from_toon('"\\x"'), "Invalid escape")
  expect_error(from_toon('"\\ud83d\\ude00"'), "surrogate")
  expect_error(from_toon('"abc'), "Unterminated")
  expect_error(from_toon('"a" b'), "after a closing quote")
  expect_error(from_toon('"\\u0000"'), "cannot be represented")
})

test_that("decoded strings are marked UTF-8", {
  expect_identical(Encoding(from_toon("k: caf\u00e9")$k), "UTF-8")
  expect_identical(Encoding(names(from_toon("caf\u00e9: 1"))), "UTF-8")
})

test_that("invalid UTF-8 is an error", {
  expect_error(from_toon(as.raw(c(0x6b, 0x3a, 0x20, 0xff))), "Invalid UTF-8")
  expect_identical(from_toon(charToRaw("k: v")), list(k = "v"))
})
