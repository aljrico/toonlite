#!/usr/bin/env Rscript
# Benchmark write performance
#
# Usage: Rscript bench_write.R [iterations]

library(toonlite)

args <- commandArgs(trailingOnly = TRUE)
iterations <- if (length(args) > 0) as.integer(args[1]) else 3

cat("=== toonlite Write Benchmarks ===\n")
cat("Iterations:", iterations, "\n\n")

# Benchmark function
bench_write <- function(data, write_fn, name, tmp_ext = ".toon") {
  tmp <- tempfile(fileext = tmp_ext)
  on.exit(unlink(tmp))

  # Warmup
  write_fn(data, tmp)
  size_mb <- file.info(tmp)$size / 1024 / 1024

  # Timed runs
  times <- numeric(iterations)
  for (i in seq_len(iterations)) {
    gc(verbose = FALSE)
    start <- Sys.time()
    write_fn(data, tmp)
    times[i] <- as.numeric(Sys.time() - start, units = "secs")
  }

  median_time <- median(times)
  throughput <- size_mb / median_time

  cat(sprintf("  %-40s %6.2f MB  %6.3f sec  %7.2f MB/s\n",
              name, size_mb, median_time, throughput))

  list(
    name = name,
    size_mb = size_mb,
    median_sec = median_time,
    throughput_mbps = throughput
  )
}

# Generate test data
cat("Generating test data...\n")

# Small data.frame
df_small <- data.frame(
  id = 1:1000,
  name = paste0("item_", 1:1000),
  value = runif(1000),
  active = sample(c(TRUE, FALSE), 1000, replace = TRUE)
)

# Medium data.frame
df_medium <- data.frame(
  id = 1:100000,
  name = paste0("item_", 1:100000),
  value = runif(100000),
  active = sample(c(TRUE, FALSE), 100000, replace = TRUE),
  category = sample(LETTERS, 100000, replace = TRUE)
)

# Large data.frame
df_large <- data.frame(
  id = 1:1000000,
  name = paste0("item_", 1:1000000),
  value = runif(1000000),
  active = sample(c(TRUE, FALSE), 1000000, replace = TRUE)
)

# Nested list
nested_small <- lapply(1:100, function(i) {
  list(id = i, data = list(x = runif(1), y = runif(1)))
})
names(nested_small) <- paste0("obj", 1:100)

nested_medium <- lapply(1:1000, function(i) {
  list(id = i, data = list(x = runif(1), y = runif(1), z = runif(1)))
})
names(nested_medium) <- paste0("obj", 1:1000)

cat("Done.\n\n")

# Tabular write benchmarks
cat("Tabular write (write_toon_df):\n")
bench_write(df_small, write_toon_df, "1K rows x 4 cols")
bench_write(df_medium, write_toon_df, "100K rows x 5 cols")
bench_write(df_large, write_toon_df, "1M rows x 4 cols")

# General write benchmarks
cat("\nGeneral write (write_toon):\n")
bench_write(nested_small, write_toon, "100 nested objects")
bench_write(nested_medium, write_toon, "1000 nested objects")

# to_toon string benchmarks
cat("\nString serialization (to_toon):\n")

bench_to_toon <- function(data, name) {
  # Warmup
  invisible(to_toon(data))

  times <- numeric(iterations)
  for (i in seq_len(iterations)) {
    gc(verbose = FALSE)
    start <- Sys.time()
    result <- to_toon(data)
    times[i] <- as.numeric(Sys.time() - start, units = "secs")
  }

  median_time <- median(times)
  size_mb <- nchar(result) / 1024 / 1024

  cat(sprintf("  %-40s %6.2f MB  %6.3f sec\n",
              name, size_mb, median_time))
}

bench_to_toon(df_small, "1K rows data.frame")
bench_to_toon(df_medium, "100K rows data.frame")
bench_to_toon(nested_small, "100 nested objects")

cat("\nDone!\n")
