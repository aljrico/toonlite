# Object decoding

test_that("objects decode to named lists in document order", {
  result <- from_toon("name: Alice\nage: 30\nperson:\n  address:\n    zip: 10001")
  expect_identical(result, list(name = "Alice", age = 30L,
                                person = list(address = list(zip = 10001L))))
})

test_that("empty objects are named lists of length 0", {
  expect_identical(from_toon("data:"), list(data = setNames(list(), character())))
  expect_identical(from_toon(""), setNames(list(), character()))
})

test_that("keys: quoted, dotted, empty and unusual", {
  expect_identical(from_toon('"key with spaces": value'), list(`key with spaces` = "value"))
  expect_identical(from_toon("user.name: Ada"), list(user.name = "Ada"))
  expect_identical(from_toon(": 1"), setNames(list(1L), ""))
  expect_identical(from_toon("foo-bar[2]: 1,2"), list(`foo-bar` = 1:2))
  expect_identical(from_toon('"a:b": 1'), list(`a:b` = 1L))
})

test_that("objects as list items", {
  toon <- "items[2]:\n  - id: 1\n    tags[2]: a,b\n  - id: 2\n    sub:\n      x: 1"
  expect_identical(from_toon(toon), list(items = list(
    list(id = 1L, tags = c("a", "b")),
    list(id = 2L, sub = list(x = 1L))
  )))
  expect_identical(from_toon("items[1]:\n  -"),
                   list(items = list(setNames(list(), character()))))
})

test_that("duplicate keys error in strict mode, last wins otherwise", {
  toon <- "key: 1\nother: 0\nkey: 3"
  expect_error(from_toon(toon), "Duplicate key 'key'")
  expect_identical(from_toon(toon, strict = FALSE), list(key = 3L, other = 0L))
})

test_that("allow_duplicate_keys is deprecated", {
  expect_warning(out <- from_toon("key: 1\nkey: 2", strict = FALSE,
                                  allow_duplicate_keys = TRUE),
                 "`allow_duplicate_keys` is deprecated")
  expect_identical(out, list(key = 2L))
  expect_warning(expect_error(from_toon("key: 1\nkey: 2", allow_duplicate_keys = TRUE),
                              "Duplicate key"),
                 "`allow_duplicate_keys` is deprecated")
})

test_that("deep nesting has a limit", {
  nest <- function(n) paste(vapply(0:n, function(i) paste0(strrep("  ", i), "k:"), ""),
                            collapse = "\n")
  expect_true(is.list(from_toon(nest(1000))))
  expect_error(from_toon(nest(1001)), "deeper than 1000 levels")
})
