# Comments (spec 5.1): full-line `#` only, after spaces. Nothing else is a
# comment.

test_that("full-line # comments are skipped anywhere", {
  expect_identical(from_toon("# head\nkey: 42\n# tail"), list(key = 42L))
  expect_identical(from_toon("a:\n  # inside\n  b: 1"), list(a = list(b = 1L)))
  expect_identical(from_toon("t[2]{x}:\n  1\n    # between rows\n  2"),
                   list(t = list(list(x = 1L), list(x = 2L))))
  expect_identical(from_toon("# only comments"), setNames(list(), character()))
})

test_that("# inside a line is content", {
  expect_identical(from_toon("key: 42 # not a comment"), list(key = "42 # not a comment"))
  expect_identical(from_toon('key: "value # kept"'), list(key = "value # kept"))
  expect_identical(from_toon("\"#k\": 1"), list(`#k` = 1L))
})

test_that("// is not a comment", {
  expect_error(from_toon("// note\nkey: 42"), "Expected 'key: value'")
  expect_identical(from_toon("key: 42 // note"), list(key = "42 // note"))
})

test_that("a tab-indented # line is not a comment", {
  expect_error(from_toon("a:\n\t# x\n  b: 1"), "Tab")
})

test_that("allow_comments is deprecated and comments stay skipped", {
  expect_warning(out <- from_toon("# c\nkey: 42", allow_comments = FALSE),
                 "`allow_comments` is deprecated")
  expect_identical(out, list(key = 42L))
  expect_silent(from_toon("# c\nkey: 42", allow_comments = TRUE))
})
