# Tests for comment handling

test_that("line comments with # are ignored", {
  toon <- "# This is a comment\nkey: 42"
  result <- from_toon(toon)

  expect_equal(result$key, 42L)
})

test_that("line comments with // are ignored", {
  toon <- "// This is a comment\nkey: 42"
  result <- from_toon(toon)

  expect_equal(result$key, 42L)
})

test_that("trailing comments after # are stripped", {
  toon <- "key: 42 # inline comment"
  result <- from_toon(toon)

  expect_equal(result$key, 42L)
})

test_that("trailing comments after // are stripped", {
  toon <- "key: 42 // inline comment"
  result <- from_toon(toon)

  expect_equal(result$key, 42L)
})

test_that("comments inside strings are preserved", {
  toon <- 'key: "value # not a comment"'
  result <- from_toon(toon)

  expect_equal(result$key, "value # not a comment")
})

test_that("allow_comments=FALSE rejects comments", {
  toon <- "# comment\nkey: 42"

  # With comments disallowed, # at start should fail
  expect_error(
    from_toon(toon, allow_comments = FALSE),
    regexp = NULL  # Any error
  )
})

test_that("multiple comment lines work", {
  toon <- "# Comment 1\n# Comment 2\nkey: 42\n# Comment 3"
  result <- from_toon(toon)

  expect_equal(result$key, 42L)
})
