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
  toon <- "items[5]:\n  - 1\n  - 2\n  - 3\n  - 4\n  - 5"

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

collect_items <- function(lines, ...) {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines(lines, tmp)
  batches <- list()
  toon_stream_items(tmp, callback = function(b) batches[[length(batches) + 1L]] <<- b, ...)
  batches
}

test_that("toon_stream_items streams every array form in batches", {
  # list form, root array
  b <- collect_items(c("[5]:", "  - 1", "  - a: 1", "    b[2]: x,y", "  - [2]: 1,2", "  -", "  - z"),
                     batch_size = 2L)
  expect_identical(lengths(b), c(2L, 2L, 1L))
  expect_identical(unlist(b, recursive = FALSE), list(
    1L, list(a = 1L, b = c("x", "y")), 1:2, setNames(list(), character()), "z"
  ))
  # tabular rows arrive as row objects
  b <- collect_items(c("n: 1", "t[3]{id,g{x}}:", "  1,a", "  2,b", "  3,c", "z: 2"),
                     key = "t", batch_size = 2L)
  expect_identical(b, list(
    list(list(id = 1L, g = list(x = "a")), list(id = 2L, g = list(x = "b"))),
    list(list(id = 3L, g = list(x = "c")))
  ))
  # inline values, and simplify applies inside items
  expect_identical(collect_items("x[3]: 1,null,c", key = "x"), list(list(1L, NULL, "c")))
  expect_identical(collect_items(c("[1]:", "  - [2]: 1,2"), simplify = FALSE),
                   list(list(list(1L, 2L))))
  # empty arrays never call back
  expect_identical(collect_items("x: []", key = "x"), list())
  expect_identical(collect_items("[]"), list())
})

test_that("toon_stream_items reports a wrong target", {
  expect_error(collect_items("a: 1"), "root is not an array")
  expect_error(collect_items("[1]: 1", key = "a"), "requires the document root to be an object")
  expect_error(collect_items("a: 1", key = "b"), "Key not found: b")
  expect_error(collect_items("a: 1", key = "a"), "Field 'a' is not an array")
  expect_error(collect_items(c("a[1:]{x}:", "  k: 1"), key = "a"), "keyed table")
  # Only fields of the root object qualify
  expect_error(collect_items(c("o:", "  a[1]: 1"), key = "a"), "Key not found: a")
})

test_that("toon_stream_items validates the whole document", {
  # A small array is checked before its only batch is delivered
  expect_error(collect_items(c("x[3]:", "  - 1", "  - 2"), key = "x"), "Declared 3 list items")
  expect_warning(out <- collect_items(c("x[3]:", "  - 1", "  - 2"), key = "x",
                                      strict = FALSE),
                 "Declared 3 list items but found 2")
  expect_identical(out, list(list(1L, 2L)))
  # Content after the target is decoded too
  calls <- 0L
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines(c("x[2]:", "  - 1", "  - 2", "y: \"bad"), tmp)
  expect_error(toon_stream_items(tmp, key = "x", batch_size = 1L,
                                 callback = function(b) calls <<- calls + 1L),
               "Unterminated string")
  expect_identical(calls, 2L)
})

test_that("toon_stream_items propagates callback errors", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines(c("[3]: 1,2,3"), tmp)
  expect_error(toon_stream_items(tmp, callback = function(b) stop("boom")), "boom")
  expect_warning(toon_stream_items(tmp, callback = function(b) warning("careful")), "careful")
})

test_that("toon_stream_items handles many batches", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  n <- 25000L
  write_toon(list(items = lapply(seq_len(n), function(i) list(id = i, tags = c("a", "b"), s = NULL))), tmp)
  total <- 0L
  last <- NULL
  toon_stream_items(tmp, key = "items", batch_size = 1000L, callback = function(b) {
    total <<- total + length(b)
    last <<- b[[length(b)]]
  })
  expect_identical(total, n)
  expect_identical(last, list(id = n, tags = c("a", "b"), s = NULL))
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

  write_toon_df(df, f_df)

  chunks <- list(df[1:3, ], df[4:8, ])
  k <- 0L
  toon_stream_write_rows(f_stream, schema = names(df), row_source = function() {
    k <<- k + 1L
    if (k <= length(chunks)) chunks[[k]] else NULL
  })

  read_text <- function(f) readChar(f, file.size(f), useBytes = TRUE)
  expect_identical(read_text(f_stream), read_text(f_df))
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

  expect_identical(readLines(tmp, warn = FALSE),
                   c("[1]{s,f,d}:", '  "a\\u0001b",lvl,null'))
  result <- read_toon_df(tmp)
  expect_identical(result$s, "a\001b")
  expect_identical(result$f, "lvl")
  expect_true(is.na(result$d))
})

test_that("toon_stream_rows honours strict like read_toon_df", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines("[2]{x}:\n\t1\n\t2", tmp)

  expect_error(read_toon_df(tmp, strict = TRUE), "Tab characters")
  expect_error(toon_stream_rows(tmp, callback = function(b) NULL, strict = TRUE),
               "Tab characters")

  got <- NULL
  toon_stream_rows(tmp, callback = function(b) got <<- b, strict = FALSE)
  expect_identical(got, read_toon_df(tmp, strict = FALSE))
})

test_that("toon_stream_rows applies col_types to every batch", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines("[4]{x,y}:\n  1, 2\n  3, 4\n  5, 6\n  7, 8", tmp)

  types <- character()
  toon_stream_rows(tmp, batch_size = 2L, col_types = c(x = "double"),
                   callback = function(b) types <<- c(types, typeof(b$x)))
  expect_identical(types, c("double", "double"))
})

test_that("toon_stream_rows propagates callback errors and keeps R usable", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines(c("[4]{x}:", "  1", "  2", "  3", "  4"), tmp)
  calls <- 0L
  expect_error(
    toon_stream_rows(tmp, batch_size = 1L, callback = function(b) {
      calls <<- calls + 1L
      if (calls == 2L) stop("boom from callback")
    }),
    "boom from callback"
  )
  expect_identical(calls, 2L)
  # Warnings and other conditions pass through unchanged too
  expect_warning(
    toon_stream_rows(tmp, batch_size = 4L, callback = function(b) warning("careful")),
    "careful"
  )
  got <- tryCatch(
    toon_stream_rows(tmp, batch_size = 1L, callback = function(b) {
      signalCondition(structure(class = c("my_signal", "condition"),
                                list(message = "sig", call = NULL)))
    }),
    my_signal = function(c) "caught"
  )
  expect_identical(got, "caught")
})

test_that("toon_stream_rows streams in batches with the same typing as read_toon_df", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines(c("data[5]{x,y}:", "  1,a", "  2,b", "  3,c", "  4,d", "  5,e"), tmp)
  sizes <- integer()
  acc <- list()
  toon_stream_rows(tmp, key = "data", batch_size = 2L, callback = function(b) {
    sizes <<- c(sizes, nrow(b))
    acc[[length(acc) + 1L]] <<- b
  })
  expect_identical(sizes, c(2L, 2L, 1L))
  expect_identical(do.call(rbind, acc), read_toon_df(tmp, key = "data"))
})

test_that("toon_stream_write_rows writes [] for no rows and leaves the file alone on error", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  n <- toon_stream_write_rows(tmp, "x", function() NULL)
  expect_identical(n, 0)
  expect_identical(readLines(tmp, warn = FALSE), "[]")

  writeLines("keep me", tmp)
  calls <- 0L
  expect_error(
    toon_stream_write_rows(tmp, "x", function() {
      calls <<- calls + 1L
      if (calls > 1L) stop("source failed")
      data.frame(x = 1:2)
    }),
    "source failed"
  )
  expect_identical(readLines(tmp), "keep me")
})

test_that("toon_stream_write_rows reorders columns by schema and supports delimiters", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  done <- FALSE
  toon_stream_write_rows(tmp, c("a", "b c"), delimiter = "|", function() {
    if (done) return(NULL)
    done <<- TRUE
    data.frame(`b c` = c("x|y", "z"), a = 1:2, check.names = FALSE)
  })
  expect_identical(readLines(tmp, warn = FALSE),
                   c('[2|]{a|"b c"}:', '  1|"x|y"', "  2|z"))
  expect_identical(read_toon_df(tmp)[["b c"]], c("x|y", "z"))
})

test_that("toon_stream_items warns about count mismatches unless warn = FALSE", {
  tmp <- tempfile(fileext = ".toon")
  on.exit(unlink(tmp))
  writeLines(c("[3]{x}:", "  1", "  2"), tmp)
  calls <- 0L
  expect_warning(toon_stream_items(tmp, strict = FALSE, batch_size = 1L,
                                   callback = function(b) calls <<- calls + 1L),
                 "Declared 3 rows but found 2")
  # The warning comes after every batch was delivered
  expect_identical(calls, 2L)
  expect_silent(toon_stream_items(tmp, strict = FALSE, warn = FALSE,
                                  callback = function(b) NULL))
  expect_error(toon_stream_items(tmp, callback = function(b) NULL), "Declared 3 rows")
})
