# Tests for round-trip serialization

test_that("primitives round-trip correctly", {
  test_values <- list(
    NULL,
    TRUE,
    FALSE,
    42L,
    -123L,
    3.14,
    "hello",
    "with\nnewline"
  )

  for (val in test_values) {
    toon <- to_toon(val)
    result <- from_toon(toon)
    expect_equal(result, val, info = paste("Value:", deparse(val)))
  }
})

test_that("vectors round-trip correctly", {
  # Integer vector
  v <- c(1L, 2L, 3L)
  toon <- to_toon(v)
  result <- from_toon(toon)
  expect_equal(result, v)

  # Double vector
  v <- c(1.1, 2.2, 3.3)
  toon <- to_toon(v)
  result <- from_toon(toon)
  expect_equal(result, v)

  # Character vector
  v <- c("a", "b", "c")
  toon <- to_toon(v)
  result <- from_toon(toon)
  expect_equal(result, v)

  # Logical vector
  v <- c(TRUE, FALSE, TRUE)
  toon <- to_toon(v)
  result <- from_toon(toon)
  expect_equal(result, v)
})

test_that("named list round-trips correctly", {
  obj <- list(
    name = "test",
    value = 42L,
    active = TRUE
  )

  toon <- to_toon(obj)
  result <- from_toon(toon)

  expect_equal(result$name, obj$name)
  expect_equal(result$value, obj$value)
  expect_equal(result$active, obj$active)
})

test_that("nested structures round-trip correctly", {
  obj <- list(
    level1 = list(
      level2 = list(
        value = 42L
      )
    )
  )

  toon <- to_toon(obj)
  result <- from_toon(toon)

  expect_equal(result$level1$level2$value, 42L)
})

test_that("data.frame round-trips correctly", {
  df <- data.frame(
    x = 1:3,
    y = c("a", "b", "c"),
    z = c(TRUE, FALSE, TRUE),
    stringsAsFactors = FALSE
  )

  tmp <- tempfile(fileext = ".toon")
  write_toon_df(df, tmp)
  result <- read_toon_df(tmp)
  unlink(tmp)

  expect_equal(nrow(result), nrow(df))
  expect_equal(ncol(result), ncol(df))
  expect_equal(result$x, df$x)
  expect_equal(result$y, df$y)
  expect_equal(result$z, df$z)
})

test_that("data.frame with NA round-trips correctly", {
  df <- data.frame(
    x = c(1L, NA, 3L),
    y = c("a", NA, "c"),
    stringsAsFactors = FALSE
  )

  tmp <- tempfile(fileext = ".toon")
  write_toon_df(df, tmp)
  result <- read_toon_df(tmp)
  unlink(tmp)

  expect_true(is.na(result$x[2]))
  expect_true(is.na(result$y[2]))
  expect_equal(result$x[1], 1L)
  expect_equal(result$y[3], "c")
})

test_that("vector with NA round-trips correctly", {
  v <- c(1L, NA, 3L)
  toon <- to_toon(v)
  result <- from_toon(toon)

  expect_equal(result[1], 1L)
  expect_true(is.na(result[2]))
  expect_equal(result[3], 3L)
})
