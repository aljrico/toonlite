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

test_that("toon_stream_write_rows output reads back like write_toon_df", {
  df <- data.frame(
    d = c(1, 2.5, NaN, Inf, -Inf, NA, 1e20, 0.1),
    s = c("a\001b", "q\"uote", NA, "tab\there", "new\nline", "back\\slash",
          "bell\a", "plain"),
    f = factor(c("u", "v", NA, "u", "v", "u", "v", "u")),
    i = c(1L, NA, 3L, 4L, 5L, 6L, 7L, 8L),
    l = c(TRUE, NA, FALSE, TRUE, TRUE, FALSE, NA, TRUE),
    stringsAsFactors = FALSE
  )
  f_df <- tempfile(fileext = ".toon")
  f_stream <- tempfile(fileext = ".toon")
  on.exit(unlink(c(f_df, f_stream)))

  write_toon_df(df, f_df, strict = FALSE)

  chunks <- list(df[1:3, ], df[4:8, ])
  k <- 0L
  toon_stream_write_rows(f_stream, schema = names(df), row_source = function() {
    k <<- k + 1L
    if (k <= length(chunks)) chunks[[k]] else NULL
  })

  # Row lines are byte-identical; only the header's row count is zero-padded
  expect_identical(readLines(f_stream)[-1], readLines(f_df)[-1])
  expect_identical(read_toon_df(f_stream), read_toon_df(f_df))
})

test_that("toon_stream_write_rows escapes control chars, writes factor levels, nulls Inf", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))

  batch <- data.frame(s = "a\001b", f = factor("lvl"), d = Inf,
                      stringsAsFactors = FALSE)
  done <- FALSE
  toon_stream_write_rows(tmp, schema = names(batch), row_source = function() {
    if (done) return(NULL)
    done <<- TRUE
    batch
  })

  expect_identical(readLines(tmp)[2], '  "a\\u0001b", "lvl", null')
  result <- read_toon_df(tmp)
  expect_identical(result$s, "a\001b")
  expect_identical(result$f, "lvl")
  expect_true(is.na(result$d))
})
