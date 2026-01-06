# Tests for validation

test_that("validate_toon returns TRUE for valid TOON", {
  expect_true(validate_toon("null"))
  expect_true(validate_toon("true"))
  expect_true(validate_toon("42"))
  expect_true(validate_toon('"hello"'))
  expect_true(validate_toon("key: value"))
  expect_true(validate_toon("key:\n  nested: true"))
  expect_true(validate_toon("[3]:\n  1\n  2\n  3"))
})

test_that("validate_toon returns FALSE with error attribute for invalid TOON", {
  # Invalid: nested value without proper structure
  result <- validate_toon("key:\n  nested\n bad_indent")
  expect_false(result)
  expect_true(!is.null(attr(result, "error")))

  err <- attr(result, "error")
  expect_true("message" %in% names(err))
  expect_true("line" %in% names(err))
  expect_true("type" %in% names(err))
})

test_that("validate_toon handles file validation", {
  # Create temp file with valid TOON

  tmp <- tempfile(fileext = ".toon")
  writeLines('name: "test"\nvalue: 42', tmp)
  on.exit(unlink(tmp))

  expect_true(validate_toon(tmp, is_file = TRUE))
})

test_that("validate_toon returns FALSE for missing file", {
  result <- validate_toon("/nonexistent/path.toon", is_file = TRUE)
  expect_false(result)

  err <- attr(result, "error")
  expect_equal(err$type, "io_error")
  expect_true(grepl("not found", err$message, ignore.case = TRUE))
})

test_that("validate_toon respects strict mode", {
  # Comments allowed by default

  expect_true(validate_toon("# comment\nkey: value"))

  # With allow_comments = FALSE, comments should fail
  result <- validate_toon("# comment\nkey: value", allow_comments = FALSE)
  expect_false(result)
})

test_that("assert_toon returns invisibly TRUE for valid TOON", {

  expect_invisible(assert_toon("null"))
  expect_true(assert_toon("key: value"))
})
test_that("assert_toon throws for invalid TOON", {
  expect_error(assert_toon("key:\n  nested\n bad_indent"), class = "toonlite_parse_error")
})

test_that("assert_toon throws for missing file", {
  expect_error(assert_toon("/nonexistent/path.toon", is_file = TRUE))
})

# format_toon tests

test_that("format_toon formats valid TOON", {
  input <- "key:1"
  result <- format_toon(input)
  expect_type(result, "character")
  expect_true(nchar(result) > 0)
})

test_that("format_toon respects indent parameter", {
  input <- "obj:\n  key: value"
  result2 <- format_toon(input, indent = 2L)
  result4 <- format_toon(input, indent = 4L)
  expect_type(result2, "character")
  expect_type(result4, "character")
})

test_that("format_toon works with files", {
  tmp <- tempfile(fileext = ".toon")
  writeLines("key: value", tmp)
  on.exit(unlink(tmp))

  result <- format_toon(tmp, is_file = TRUE)
  expect_type(result, "character")
})

# toon_peek tests

test_that("toon_peek returns structure info", {
  tmp <- tempfile(fileext = ".toon")
  writeLines('name: "Alice"\nage: 30', tmp)
  on.exit(unlink(tmp))

  result <- toon_peek(tmp)
  expect_type(result, "list")
  expect_true("type" %in% names(result))
  expect_true("preview" %in% names(result))
})

test_that("toon_peek detects object type", {
  tmp <- tempfile(fileext = ".toon")
  writeLines('key: value', tmp)
  on.exit(unlink(tmp))

  result <- toon_peek(tmp)
  expect_equal(result$type, "object")
})

test_that("toon_peek detects array type", {
  tmp <- tempfile(fileext = ".toon")
  writeLines('[3]:\n  1\n  2\n  3', tmp)
  on.exit(unlink(tmp))

  result <- toon_peek(tmp)
  expect_true(result$type %in% c("array", "tabular_array"))
})

test_that("toon_peek respects n parameter", {
  tmp <- tempfile(fileext = ".toon")
  writeLines(paste0("line", 1:100, ": value"), tmp)
  on.exit(unlink(tmp))

  result10 <- toon_peek(tmp, n = 10L)
  result50 <- toon_peek(tmp, n = 50L)

  expect_true(length(result10$preview) <= 10)
  expect_true(length(result50$preview) <= 50)
})

# toon_info tests

test_that("toon_info returns structure summary", {
  tmp <- tempfile(fileext = ".toon")
  writeLines('name: "test"\ndata:\n  x: 1\n  y: 2', tmp)
  on.exit(unlink(tmp))

  result <- toon_info(tmp)
  expect_type(result, "list")
  expect_true("object_count" %in% names(result))
  expect_true("array_count" %in% names(result))
  expect_true("has_tabular" %in% names(result))
})

test_that("toon_info detects tabular arrays", {
  tmp <- tempfile(fileext = ".toon")
  writeLines('[3]{name,value}:\n  "a", 1\n  "b", 2\n  "c", 3', tmp)
  on.exit(unlink(tmp))

  result <- toon_info(tmp)
  expect_true(result$has_tabular)
  expect_equal(result$declared_rows, 3L)
})

test_that("toon_info handles non-tabular files", {
  tmp <- tempfile(fileext = ".toon")
  writeLines('simple: value', tmp)
  on.exit(unlink(tmp))

  result <- toon_info(tmp)
  expect_false(result$has_tabular)
})
