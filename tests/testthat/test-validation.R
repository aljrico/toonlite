# Tests for validation

test_that("validate_toon returns TRUE for valid TOON", {
  expect_true(validate_toon("null"))
  expect_true(validate_toon("true"))
  expect_true(validate_toon("42"))
  expect_true(validate_toon('"hello"'))
  expect_true(validate_toon("key: value"))
})
