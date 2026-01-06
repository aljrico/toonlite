# Tests for streaming functionality

test_that("toon_stream_rows calls callback with batches", {
  toon <- "[10]{x,y}:\n  1, 2\n  3, 4\n  5, 6\n  7, 8\n  9, 10\n  11, 12\n  13, 14\n  15, 16\n  17, 18\n  19, 20"

  tmp <- tempfile(fileext = ".toon")
  writeLines(toon, tmp)

  batches <- list()
  callback <- function(batch) {
    batches <<- c(batches, list(batch))
  }

  toon_stream_rows(tmp, callback = callback, batch_size = 3L)

  expect_true(length(batches) >= 3)  # 10 rows / 3 per batch

  # Check first batch
  expect_equal(nrow(batches[[1]]), 3)
  expect_equal(batches[[1]]$x[1], 1L)

  unlink(tmp)
})

test_that("toon_stream_items processes list items", {
  # Create a TOON file with array
  toon <- "items:\n  - 1\n  - 2\n  - 3\n  - 4\n  - 5"

  tmp <- tempfile(fileext = ".toon")
  writeLines(toon, tmp)

  collected <- c()
  callback <- function(batch) {
    collected <<- c(collected, unlist(batch))
  }

  toon_stream_items(tmp, key = "items", callback = callback, batch_size = 2L)

  expect_equal(length(collected), 5)
  expect_equal(collected, c(1L, 2L, 3L, 4L, 5L))

  unlink(tmp)
})

test_that("toon_stream_write_rows creates valid TOON", {
  tmp <- tempfile(fileext = ".toon")

  batch_num <- 0
  batches <- list(
    data.frame(x = 1:3, y = c("a", "b", "c"), stringsAsFactors = FALSE),
    data.frame(x = 4:5, y = c("d", "e"), stringsAsFactors = FALSE),
    NULL
  )

  row_source <- function() {
    batch_num <<- batch_num + 1
    batches[[batch_num]]
  }

  rows_written <- toon_stream_write_rows(
    tmp,
    schema = c("x", "y"),
    row_source = row_source
  )

  expect_equal(rows_written, 5)

  # Verify we can read it back
  result <- read_toon_df(tmp)
  expect_equal(nrow(result), 5)
  expect_equal(result$x, c(1L, 2L, 3L, 4L, 5L))
  expect_equal(result$y, c("a", "b", "c", "d", "e"))

  unlink(tmp)
})
