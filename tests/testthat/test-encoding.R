# Encoder behaviour specific to R values. The spec's own encode fixtures run in
# test-spec-conformance.R.

toon <- function(...) unclass(to_toon(...))

test_that("primitives encode canonically", {
  expect_identical(toon(NULL), "null")
  expect_identical(toon(TRUE), "true")
  expect_identical(toon(FALSE), "false")
  expect_identical(toon(42L), "42")
  expect_identical(toon(-123L), "-123")
  expect_identical(toon(3.14), "3.14")
  expect_identical(toon(1), "1")
  expect_identical(toon(-0), "0")
  expect_identical(toon(1e21), "1e+21")
  expect_identical(toon(1e-7), "1e-7")
  expect_identical(toon(0.1 + 0.2), "0.30000000000000004")
  expect_identical(toon(123456.7), "123456.7")
  expect_identical(toon(.Machine$double.xmax), "1.7976931348623157e+308")
  expect_identical(toon(5e-324), "5e-324")
})

test_that("strings are quoted only when needed", {
  expect_identical(toon("hello"), "hello")
  expect_identical(toon("hello world"), "hello world")
  expect_identical(toon(""), '""')
  expect_identical(toon("42"), '"42"')
  expect_identical(toon("true"), '"true"')
  expect_identical(toon("a,b"), '"a,b"')
  expect_identical(toon("a,b", delimiter = "|"), "a,b")
  expect_identical(toon("line1\nline2"), '"line1\\nline2"')
  expect_identical(toon("bell\a"), '"bell\\u0007"')
  expect_identical(toon("caf\u00e9"), "caf\u00e9")
})

test_that("missing and non-finite values encode as null", {
  expect_identical(toon(NA), "null")
  expect_identical(toon(NA_real_), "null")
  expect_identical(toon(NA_character_), "null")
  expect_identical(toon(c(1, NA, NaN, Inf, -Inf)), "[5]: 1,null,null,null,null")
})

test_that("vectors are inline arrays, I() makes length-1 arrays", {
  expect_identical(toon(c(1L, 2L, 3L)), "[3]: 1,2,3")
  expect_identical(toon(c("a", "b")), "[2]: a,b")
  expect_identical(toon(c(a = 1, b = 2)), "[2]: 1,2")
  expect_identical(toon(character()), "[]")
  expect_identical(toon(list(x = I(5))), "x[1]: 5")
  expect_identical(toon(list(tags = c("a", "b c"), n = 2L)), "tags[2]: a,b c\nn: 2")
})

test_that("lists map to objects and arrays", {
  expect_identical(toon(list()), "[]")
  expect_identical(toon(setNames(list(), character())), "")
  expect_identical(toon(list(a = list())), "a: []")
  expect_identical(toon(list(a = setNames(list(), character()))), "a:")
  expect_identical(toon(list(a = list(b = 1, c = NULL))), "a:\n  b: 1\n  c: null")
  expect_identical(toon(list(1, "x", NULL)), "[3]: 1,x,null")
  expect_error(to_toon(list(a = 1, a = 2)), "Duplicate name 'a'")
})

test_that("factors, dates and times encode as strings", {
  expect_identical(toon(factor(c("a", "b", NA))), "[3]: a,b,null")
  expect_identical(toon(as.Date("2024-01-15")), "2024-01-15")
  expect_identical(toon(as.Date(c("2024-01-15", NA))), "[2]: 2024-01-15,null")
  expect_identical(toon(as.POSIXct("2024-01-15 10:30:00", tz = "UTC")),
                   '"2024-01-15T10:30:00Z"')
  expect_identical(toon(as.POSIXct(-0.5, origin = "1970-01-01", tz = "UTC")),
                   '"1969-12-31T23:59:59Z"')
})

test_that("unsupported types encode as null", {
  expect_identical(toon(list(f = mean, r = as.raw(1))), "f: null\nr: null")
})

test_that("data frames encode as tabular arrays", {
  df <- data.frame(id = 1:2, name = c("Ada", "Bob, Jr."), score = c(1.5, NA))
  expect_identical(toon(df), '[2]{id,name,score}:\n  1,Ada,1.5\n  2,"Bob, Jr.",null')
  expect_identical(toon(df, delimiter = "\t"),
                   "[2\t]{id\tname\tscore}:\n  1\tAda\t1.5\n  2\tBob, Jr.\tnull")
  expect_identical(toon(list(rows = df[0, ])), "rows: []")

  nested <- data.frame(id = 1:2)
  nested$geo <- data.frame(lat = c(1.5, 2), lon = c(3, 4))
  expect_identical(toon(nested), "[2]{id,geo{lat,lon}}:\n  1,1.5,3\n  2,2,4")

  # A list column with non-primitive cells falls back to list form
  lc <- data.frame(id = 1:2)
  lc$tags <- list(c("a", "b"), "c")
  expect_identical(toon(lc), "[2]:\n  - id: 1\n    tags[2]: a,b\n  - id: 2\n    tags: c")
})

test_that("indent and delimiter are validated", {
  expect_identical(toon(list(a = list(b = 1)), indent = 4L), "a:\n    b: 1")
  expect_error(to_toon(1, indent = 0), "positive whole number")
  expect_error(to_toon(1, delimiter = ";"), "`delimiter` must be")
  expect_identical(toon(c(1, 2), delimiter = "pipe"), "[2|]: 1|2")
})

test_that("deprecated encoder arguments warn and are ignored", {
  expect_warning(out <- to_toon(list(a = 1, b = 2), pretty = FALSE), "`pretty` is deprecated")
  expect_identical(unclass(out), "a: 1\nb: 2")
  expect_warning(out <- to_toon(NaN, strict = TRUE), "`strict` is deprecated")
  expect_identical(unclass(out), "null")
  expect_warning(to_toon(1, allow_comments = TRUE), "`allow_comments` is deprecated")
  expect_silent(to_toon(NaN))
})

test_that("write_toon writes the same text without a trailing newline", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  x <- list(users = data.frame(id = 1:3, ok = c(TRUE, FALSE, NA)), n = 3L)
  write_toon(x, tmp)
  expect_identical(readChar(tmp, file.size(tmp), useBytes = TRUE), toon(x))
  expect_identical(read_toon_df(tmp, key = "users")$ok, c(TRUE, FALSE, NA))
})

test_that("write_toon removes the file when encoding fails", {
  tmp <- tempfile(fileext = ".toon")
  expect_error(write_toon(list(a = 1, a = 2), tmp), "Duplicate name")
  expect_false(file.exists(tmp))
})

test_that("json_to_toon does not warn about allow_comments", {
  skip_if_not_installed("jsonlite")
  expect_silent(json_to_toon('{"a": 1, "b": [1, 2]}'))
  expect_identical(unclass(json_to_toon('{"a": 1, "b": [1, 2]}')), "a: 1\nb[2]: 1,2")
})
