# Conformance tests using fixture files

get_fixture_path <- function(name) {
system.file("extdata", "conformance", name, package = "toonlite")
}

# Primitives conformance

test_that("primitives fixture parses correctly", {
  path <- get_fixture_path("primitives.toon")
  skip_if(path == "", "Fixture not installed")

  data <- read_toon(path)

  expect_null(data$null_value)
  expect_true(data$bool_true)
  expect_false(data$bool_false)
  expect_equal(data$int_zero, 0L)
  expect_equal(data$int_positive, 42L)
  expect_equal(data$int_negative, -17L)
  expect_equal(data$float_simple, 3.14)
  expect_equal(data$float_scientific, 1.23e10)
  expect_equal(data$float_negative, -0.5)
  expect_equal(data$string_simple, "hello")
  expect_equal(data$string_empty, "")
  expect_equal(data$string_escapes, "line1\nline2\ttab")
})

# Objects conformance

test_that("objects fixture parses correctly", {
  path <- get_fixture_path("objects.toon")
  skip_if(path == "", "Fixture not installed")

  data <- read_toon(path)

  expect_type(data$simple, "list")
  expect_equal(data$simple$key1, "value1")
  expect_equal(data$simple$key2, 42L)

  expect_equal(data$nested$level1$level2$level3, "deep")

  expect_equal(data$mixed$string, "text")
  expect_equal(data$mixed$number, 123L)
  expect_true(data$mixed$boolean)
  expect_null(data$mixed$null_val)
})

# Arrays conformance

test_that("arrays fixture parses correctly", {
  path <- get_fixture_path("arrays.toon")
  skip_if(path == "", "Fixture not installed")

  data <- read_toon(path)

  expect_equal(data$integers, 1:5)
  expect_equal(data$strings, c("alpha", "beta", "gamma"))
  expect_type(data$mixed, "list")
  expect_equal(length(data$mixed), 4L)
})

# Tabular conformance

test_that("tabular fixture parses correctly as data.frame", {
  path <- get_fixture_path("tabular.toon")
  skip_if(path == "", "Fixture not installed")

  df <- read_toon_df(path)

  expect_s3_class(df, "data.frame")
  expect_equal(nrow(df), 5L)
  expect_equal(ncol(df), 3L)
  expect_equal(names(df), c("name", "age", "active"))
  expect_equal(df$name, c("Alice", "Bob", "Charlie", "Diana", "Eve"))
  expect_equal(df$age, c(30L, 25L, 35L, 28L, 22L))
  expect_equal(df$active, c(TRUE, FALSE, TRUE, NA, TRUE))
})

# Comments conformance

test_that("comments fixture parses correctly", {
  path <- get_fixture_path("comments.toon")
  skip_if(path == "", "Fixture not installed")

  data <- read_toon(path, allow_comments = TRUE)

  expect_equal(data$key1, "value1")
  expect_equal(data$key2, 42L)
  expect_true(data$key3$nested)
})

test_that("comments fixture fails with allow_comments = FALSE", {
  path <- get_fixture_path("comments.toon")
  skip_if(path == "", "Fixture not installed")

  result <- validate_toon(path, is_file = TRUE, allow_comments = FALSE)
  expect_false(result)
})

# Edge cases conformance

test_that("edge_cases fixture parses correctly", {
  path <- get_fixture_path("edge_cases.toon")
  skip_if(path == "", "Fixture not installed")

  data <- read_toon(path)

  expect_equal(data$empty_string, "")
  expect_equal(data$large_int, 2147483647L)
  expect_equal(data$negative_large_int, -2147483647L)
  expect_type(data$float_precision, "double")
})

# Round-trip conformance

test_that("primitives round-trip correctly", {
  path <- get_fixture_path("primitives.toon")
  skip_if(path == "", "Fixture not installed")

  original <- read_toon(path)
  toon_str <- to_toon(original)
  reparsed <- from_toon(toon_str)

  expect_equal(reparsed$null_value, original$null_value)
  expect_equal(reparsed$bool_true, original$bool_true)
  expect_equal(reparsed$int_positive, original$int_positive)
  expect_equal(reparsed$string_simple, original$string_simple)
})

test_that("tabular round-trip correctly", {
  path <- get_fixture_path("tabular.toon")
  skip_if(path == "", "Fixture not installed")

  original <- read_toon_df(path)

  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))

  write_toon_df(original, tmp)
  reparsed <- read_toon_df(tmp)

  expect_equal(reparsed$name, original$name)
  expect_equal(reparsed$age, original$age)
  expect_equal(reparsed$active, original$active)
})

# Validation conformance

test_that("all conformance fixtures validate", {
  fixtures <- c("primitives.toon", "objects.toon", "arrays.toon",
                "tabular.toon", "comments.toon", "edge_cases.toon")

  for (fixture in fixtures) {
    path <- get_fixture_path(fixture)
    skip_if(path == "", paste("Fixture not installed:", fixture))

    expect_true(validate_toon(path, is_file = TRUE),
                info = paste("Fixture should validate:", fixture))
  }
})
