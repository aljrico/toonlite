# Array decoding. The spec fixtures cover the grammar; these pin the R mapping.

test_that("list-form arrays need a header", {
  expect_identical(from_toon("items[3]:\n  - 1\n  - 2\n  - 3"), list(items = 1:3))
  expect_identical(from_toon("[3]:\n  - a\n  - \"b\"\n  - c"), c("a", "b", "c"))
  # Hyphen lines under a plain `key:` are not list items
  expect_error(from_toon("items:\n  - 1\n  - 2"), "Expected 'key: value'")
})

test_that("inline arrays split on the declared delimiter", {
  expect_identical(from_toon("x[3]: 1,2,3"), list(x = 1:3))
  expect_identical(from_toon("x[2|]: a,b|c"), list(x = c("a,b", "c")))
  expect_identical(from_toon("x[2\t]: a b\tc"), list(x = c("a b", "c")))
  expect_identical(from_toon('x[3]: "",,"[]"'), list(x = c("", "", "[]")))
})

test_that("empty arrays decode in every form", {
  empty <- list(a = list())
  expect_identical(from_toon("a: []"), empty)
  expect_identical(from_toon("a[0]:"), empty)
  expect_identical(from_toon("[]"), list())
  expect_identical(from_toon("[0]:"), list())
  expect_identical(from_toon("a[1]:\n  - []"), list(a = list(list())))
})

test_that("nested arrays", {
  toon <- "m[2]:\n  - [2]: 1,2\n  - [2]: 3,4"
  expect_identical(from_toon(toon), list(m = list(1:2, 3:4)))
  expect_identical(from_toon(toon, simplify = FALSE),
                   list(m = list(list(1L, 2L), list(3L, 4L))))
})

test_that("simplify makes atomic vectors of single-type primitive arrays", {
  expect_identical(from_toon("v[3]: 1,null,3"), list(v = c(1L, NA, 3L)))
  expect_identical(from_toon("v[3]: 1,2.5,null"), list(v = c(1, 2.5, NA)))
  expect_identical(from_toon("v[2]: true,null"), list(v = c(TRUE, NA)))
  expect_identical(from_toon("v[2]: a,null"), list(v = c("a", NA)))
  # Mixed types and all-null arrays stay lists
  expect_identical(from_toon("v[2]: true,1"), list(v = list(TRUE, 1L)))
  expect_identical(from_toon("v[2]: 1,a"), list(v = list(1L, "a")))
  expect_identical(from_toon("v[2]: null,null"), list(v = list(NULL, NULL)))
  expect_identical(from_toon("v[2]: 1,2", simplify = FALSE), list(v = list(1L, 2L)))
})

test_that("tabular arrays decode to lists of row objects", {
  toon <- "users[2]{id,name,geo{lat}}:\n  1,Ada,1.5\n  2,Bob,null"
  expect_identical(from_toon(toon), list(users = list(
    list(id = 1L, name = "Ada", geo = list(lat = 1.5)),
    list(id = 2L, name = "Bob", geo = list(lat = NULL))
  )))
})

test_that("keyed tables decode to objects of row objects", {
  toon <- "m[2:]{x,y}:\n  a: 1,2\n  \"b c\": 3,4"
  expect_identical(from_toon(toon), list(m = list(
    a = list(x = 1L, y = 2L), `b c` = list(x = 3L, y = 4L)
  )))
})

test_that("declared counts are enforced only in strict mode", {
  toon <- "[5]:\n  - 1\n  - 2\n  - 3"
  expect_error(from_toon(toon), "Declared 5 list items but found 3")
  expect_silent(out <- from_toon(toon, strict = FALSE))
  expect_identical(out, 1:3)
  expect_identical(from_toon("x[1]: a,b", strict = FALSE), list(x = c("a", "b")))
})
