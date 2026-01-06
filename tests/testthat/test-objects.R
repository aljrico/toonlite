# Tests for object parsing

test_that("simple object is parsed correctly", {
  toon <- "name: \"Alice\"\nage: 30"
  result <- from_toon(toon)

  expect_true(is.list(result))
  expect_equal(names(result), c("name", "age"))
  expect_equal(result$name, "Alice")
  expect_equal(result$age, 30L)
})

test_that("nested object is parsed correctly", {
  toon <- "person:\n  name: \"Bob\"\n  address:\n    city: \"NYC\"\n    zip: 10001"
  result <- from_toon(toon)

  expect_true(is.list(result))
  expect_equal(result$person$name, "Bob")
  expect_equal(result$person$address$city, "NYC")
  expect_equal(result$person$address$zip, 10001L)
})

test_that("empty object is parsed correctly", {
  toon <- "data:"
  result <- from_toon(toon)

  expect_true(is.list(result))
  expect_true("data" %in% names(result))
})

test_that("object with quoted keys works", {
  toon <- '"key with spaces": "value"'
  result <- from_toon(toon)

  expect_equal(names(result), "key with spaces")
  expect_equal(result[["key with spaces"]], "value")
})

test_that("duplicate keys with allow_duplicate_keys=TRUE uses last value", {
  toon <- "key: 1\nkey: 2\nkey: 3"

  # With duplicates allowed (default), should use last value
  result <- from_toon(toon, allow_duplicate_keys = TRUE)
  expect_equal(result$key, 3L)
})

test_that("duplicate keys with allow_duplicate_keys=FALSE throws error", {
  toon <- "key: 1\nkey: 2"

  expect_error(
    from_toon(toon, allow_duplicate_keys = FALSE),
    "Duplicate key"
  )
})
